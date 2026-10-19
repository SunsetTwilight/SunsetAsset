// ============================================================
//  src/model/ModelSerialize.cpp
//
//  モデル ⇔ キャッシュのチャンク。
//    MJSN  構造 (ノード、スロット、スキン、メッシュの名前 / 頂点の数 / サブメッシュ / 境界 / どの配列があるか)
//    MBIN  配列を並べたもの。MJSN に (先頭からの位置, 要素の数) を書く。位置は 4 バイトに揃える
// ============================================================
#include "sunset/asset/model/Model.h"

#include <cmath>
#include <cstring>

namespace sunset::asset
{
namespace
{
constexpr ChunkTag k_Json = chunkTag("MJSN");
constexpr ChunkTag k_Bin  = chunkTag("MBIN");

struct BlobWriter
{
	std::string bytes;
	template<typename T>
	Json put(const std::vector<T>& v)
	{
		while (bytes.size() % 4 != 0)
			bytes.push_back('\0');
		const size_t offset = bytes.size();
		bytes.append(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T));
		return Json::array({ offset, v.size() });
	}
};

struct BlobReader
{
	std::span<const char> bytes;
	bool                  ok = true;
	template<typename T>
	std::vector<T> get(const Json& ref)
	{
		if (!ref.is_array() || ref.size() != 2)
			return {};
		const size_t offset = ref[0].get<size_t>(), count = ref[1].get<size_t>();
		if (offset > bytes.size() || count > (bytes.size() - offset) / sizeof(T))
		{
			ok = false;
			return {};
		}
		std::vector<T> out(count);
		if (count)
			std::memcpy(out.data(), bytes.data() + offset, count * sizeof(T));
		return out;
	}
};

template<size_t N>
Json arr(const std::array<float, N>& a)
{
	Json j = Json::array();
	for (float v : a) j.push_back(v);
	return j;
}
template<size_t N>
void getArr(const Json& j, const char* key, std::array<float, N>& out)
{
	if (j.contains(key) && j.at(key).is_array() && j.at(key).size() == N)
		for (size_t i = 0; i < N; ++i)
			out[i] = j.at(key)[i].get<float>();
}

Json texJson(const TextureRef& t)
{
	return t.used() ? Json { { "path", t.path }, { "uvSet", t.uvSet } } : Json(nullptr);
}
TextureRef texFrom(const Json& j)
{
	TextureRef t;
	if (j.is_object())
	{
		t.path  = j.value("path", std::string());
		t.uvSet = j.value("uvSet", 0);
	}
	return t;
}
const char* alphaName(AlphaMode m) { return m == AlphaMode::Mask ? "mask" : m == AlphaMode::Blend ? "blend" : "opaque"; }
} // namespace

void writeModel(const Model& m, ArtifactWriter& out)
{
	BlobWriter blob;
	Json j = Json::object();
	j["version"] = 1;
	j["boundsMin"] = arr(m.boundsMin);
	j["boundsMax"] = arr(m.boundsMax);
	Json nodes = Json::array();
	for (const auto& n : m.nodes)
		nodes.push_back({ { "name", n.name }, { "parent", n.parent }, { "t", arr(n.translation) }, { "r", arr(n.rotation) },
		                  { "s", arr(n.scale) }, { "mesh", n.mesh }, { "skin", n.skin } });
	j["nodes"] = nodes;
	Json slots = Json::array();
	for (const auto& s : m.slots)
		slots.push_back({ { "name", s.name }, { "baseColor", arr(s.baseColor) }, { "metallic", s.metallic }, { "roughness", s.roughness },
		                  { "emissive", arr(s.emissive) }, { "alphaMode", alphaName(s.alphaMode) }, { "alphaCutoff", s.alphaCutoff },
		                  { "doubleSided", s.doubleSided }, { "baseColorTexture", texJson(s.baseColorTexture) },
		                  { "normalTexture", texJson(s.normalTexture) }, { "metallicRoughnessTexture", texJson(s.metallicRoughnessTexture) },
		                  { "occlusionTexture", texJson(s.occlusionTexture) }, { "emissiveTexture", texJson(s.emissiveTexture) } });
	j["slots"] = slots;
	Json skins = Json::array();
	for (const auto& s : m.skins)
		skins.push_back({ { "name", s.name }, { "joints", blob.put(s.joints) }, { "inverseBind", blob.put(s.inverseBind) } });
	j["skins"] = skins;
	Json meshes = Json::array();
	for (const auto& me : m.meshes)
	{
		Json subs = Json::array();
		for (const auto& sm : me.submeshes)
			subs.push_back({ sm.indexOffset, sm.indexCount, sm.slot });
		Json morphs = Json::array();
		for (const auto& mt : me.morphs)
			morphs.push_back({ { "name", mt.name }, { "weight", mt.defaultWeight }, { "positions", blob.put(mt.positions) },
			                   { "normals", blob.put(mt.normals) } });
		meshes.push_back({
			{ "name", me.name }, { "vertexCount", me.vertexCount }, { "boundsMin", arr(me.boundsMin) }, { "boundsMax", arr(me.boundsMax) },
			{ "positions", blob.put(me.positions) }, { "normals", blob.put(me.normals) }, { "tangents", blob.put(me.tangents) },
			{ "uv0", blob.put(me.uv0) }, { "uv1", blob.put(me.uv1) }, { "colors", blob.put(me.colors) },
			{ "joints", blob.put(me.joints) }, { "weights", blob.put(me.weights) }, { "indices", blob.put(me.indices) },
			{ "submeshes", subs }, { "morphs", morphs },
		});
	}
	j["meshes"] = meshes;
	out.add(k_Json, j.dump());
	out.add(k_Bin, std::move(blob.bytes));
}

bool readModel(const ArtifactReader& in, Model& out, std::string* error)
{
	const auto js = in.find(k_Json);
	if (js.empty())
	{
		if (error) *error = "モデルの構造 (MJSN) がありません";
		return false;
	}
	Json j;
	try
	{
		j = Json::parse(std::string_view(js.data(), js.size()));
		BlobReader blob { in.find(k_Bin) };
		Model m;
		getArr(j, "boundsMin", m.boundsMin);
		getArr(j, "boundsMax", m.boundsMax);
		for (const auto& n : j.at("nodes"))
		{
			ModelNode node;
			node.name   = n.value("name", std::string());
			node.parent = n.value("parent", -1);
			getArr(n, "t", node.translation);
			getArr(n, "r", node.rotation);
			getArr(n, "s", node.scale);
			node.mesh = n.value("mesh", -1);
			node.skin = n.value("skin", -1);
			m.nodes.push_back(node);
		}
		for (const auto& s : j.at("slots"))
		{
			MaterialSlot slot;
			slot.name = s.value("name", std::string());
			getArr(s, "baseColor", slot.baseColor);
			slot.metallic  = s.value("metallic", 0.0f);
			slot.roughness = s.value("roughness", 1.0f);
			getArr(s, "emissive", slot.emissive);
			const std::string am = s.value("alphaMode", std::string("opaque"));
			slot.alphaMode   = am == "mask" ? AlphaMode::Mask : am == "blend" ? AlphaMode::Blend : AlphaMode::Opaque;
			slot.alphaCutoff = s.value("alphaCutoff", 0.5f);
			slot.doubleSided = s.value("doubleSided", false);
			slot.baseColorTexture         = texFrom(s.value("baseColorTexture", Json()));
			slot.normalTexture            = texFrom(s.value("normalTexture", Json()));
			slot.metallicRoughnessTexture = texFrom(s.value("metallicRoughnessTexture", Json()));
			slot.occlusionTexture         = texFrom(s.value("occlusionTexture", Json()));
			slot.emissiveTexture          = texFrom(s.value("emissiveTexture", Json()));
			m.slots.push_back(slot);
		}
		for (const auto& s : j.at("skins"))
			m.skins.push_back({ s.value("name", std::string()), blob.get<int32_t>(s.at("joints")), blob.get<float>(s.at("inverseBind")) });
		for (const auto& me : j.at("meshes"))
		{
			ModelMesh mesh;
			mesh.name        = me.value("name", std::string());
			mesh.vertexCount = me.value("vertexCount", 0u);
			getArr(me, "boundsMin", mesh.boundsMin);
			getArr(me, "boundsMax", mesh.boundsMax);
			mesh.positions = blob.get<float>(me.at("positions"));
			mesh.normals   = blob.get<float>(me.at("normals"));
			mesh.tangents  = blob.get<float>(me.at("tangents"));
			mesh.uv0       = blob.get<float>(me.at("uv0"));
			mesh.uv1       = blob.get<float>(me.at("uv1"));
			mesh.colors    = blob.get<float>(me.at("colors"));
			mesh.joints    = blob.get<uint16_t>(me.at("joints"));
			mesh.weights   = blob.get<float>(me.at("weights"));
			mesh.indices   = blob.get<uint32_t>(me.at("indices"));
			for (const auto& sm : me.at("submeshes"))
				mesh.submeshes.push_back({ sm[0].get<uint32_t>(), sm[1].get<uint32_t>(), sm[2].get<uint32_t>() });
			for (const auto& mt : me.at("morphs"))
				mesh.morphs.push_back({ mt.value("name", std::string()), mt.value("weight", 0.0f), blob.get<float>(mt.at("positions")),
				                        blob.get<float>(mt.at("normals")) });
			m.meshes.push_back(std::move(mesh));
		}
		if (!blob.ok)
		{
			if (error) *error = "モデルの配列 (MBIN) が壊れています";
			return false;
		}
		out = std::move(m);
		return true;
	}
	catch (const std::exception& e)
	{
		if (error) *error = std::string("モデルの構造 (MJSN) を読めません: ") + e.what();
		return false;
	}
}

std::vector<std::array<float, 16>> nodeWorldMatrices(const Model& model)
{
	auto local = [](const ModelNode& n) {
		const float x = n.rotation[0], y = n.rotation[1], z = n.rotation[2], w = n.rotation[3];
		std::array<float, 16> m {};
		// R × S (列優先)。クォータニオンから回転行列
		const float r[9] = {
			1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),
			2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
			2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y),
		};
		for (int c = 0; c < 3; ++c)
			for (int row = 0; row < 3; ++row)
				m[c * 4 + row] = r[c * 3 + row] * n.scale[static_cast<size_t>(c)];
		m[12] = n.translation[0]; m[13] = n.translation[1]; m[14] = n.translation[2]; m[15] = 1;
		return m;
	};
	auto mul = [](const std::array<float, 16>& a, const std::array<float, 16>& b) {
		std::array<float, 16> r {};
		for (int c = 0; c < 4; ++c)
			for (int row = 0; row < 4; ++row)
			{
				float s = 0;
				for (int k = 0; k < 4; ++k) s += a[k * 4 + row] * b[c * 4 + k];
				r[c * 4 + row] = s;
			}
		return r;
	};
	std::vector<std::array<float, 16>> world(model.nodes.size());
	std::vector<int> done(model.nodes.size(), 0);
	// 親が先に来るとは限らないので、深さで繰り返す (循環は親を無視)
	for (size_t pass = 0; pass <= model.nodes.size(); ++pass)
	{
		bool progress = false;
		for (size_t i = 0; i < model.nodes.size(); ++i)
		{
			if (done[i]) continue;
			const int32_t p = model.nodes[i].parent;
			if (p < 0 || p >= static_cast<int32_t>(model.nodes.size()) || pass == model.nodes.size())
			{
				world[i] = local(model.nodes[i]);
				done[i] = 1;
				progress = true;
			}
			else if (done[static_cast<size_t>(p)])
			{
				world[i] = mul(world[static_cast<size_t>(p)], local(model.nodes[i]));
				done[i] = 1;
				progress = true;
			}
		}
		if (!progress) break;
	}
	return world;
}
} // namespace sunset::asset
