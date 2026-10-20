// ============================================================
//  src/model/PmxImport.cpp
//
//  MMD のモデル (PMX 2.0 / 2.1、PMD) の取り込み (自作)。モデルの型 (右手系、Y が上、メートル) にする。
//
//    座標    MMD は左手系 → Z を反転し、三角形の向きを入れ替える。1 単位 ≒ 8 cm → 0.08 倍 (設定の scale も掛ける)
//    頂点    1 つのメッシュ。MMD の頂点の番号をそのまま使う (モーフの番号と合わせる)。UV は左上が原点のまま
//    重み    BDEF1 / 2 / 4 はそのまま、SDEF は BDEF2、QDEF は BDEF4 で近似する (大きい 4 つ、合計 1)
//    材質    スロット (基本色 = 拡散色 + アルファ、両面、テクスチャ)。トゥーン / スフィア / 輪郭線 / 反射 / 環境色は
//            スロットの extras.mmd に
//    骨      ノード (0 番はメッシュのノード、骨は 1 番から)。位置は親からの相対、回転なし。スキンの骨 = 全部の骨。
//            付与親 / IK / 軸の固定 などはノードの extras.mmd に (骨の番号はノードの番号に直す)
//    モーフ  頂点モーフはメッシュのモーフ (差分を全頂点に広げる)。それ以外 (グループ / 骨 / UV / 材質 / フリップ /
//            インパルス) は Model::extras.mmd.morphs に保存だけ
//    物理    剛体 / ジョイントは Model::extras.mmd に保存だけ (座標は同じように直す)。PMX 2.1 のソフトボディは読まない
//  文字: PMX は UTF-16LE / UTF-8、PMD は Shift_JIS (Windows の変換を使う。ほかの OS では ASCII 以外を ? にする)
// ============================================================
#include "MeshBuilder.h"
#include "ModelFormats.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace sunset::asset::detail
{
namespace
{
constexpr float k_MmdUnit = 0.08f;   // MMD の 1 単位 ≒ 8 cm

// ---- バイナリの読み取り (足りなければ ok = false にして 0 を返す) ----
struct Reader
{
	const uint8_t* p = nullptr;
	size_t         n = 0, pos = 0;
	bool           ok = true;

	bool has(size_t k) const { return ok && pos + k <= n; }
	template<typename T> T get()
	{
		T v {};
		if (!has(sizeof(T))) { ok = false; return v; }
		std::memcpy(&v, p + pos, sizeof(T));
		pos += sizeof(T);
		return v;
	}
	void skip(size_t k) { if (!has(k)) ok = false; else pos += k; }
	std::array<float, 3> f3() { return { get<float>(), get<float>(), get<float>() }; }
	std::array<float, 4> f4() { return { get<float>(), get<float>(), get<float>(), get<float>() }; }
	// 番号 (1 / 2 / 4 バイト)。符号付き (-1 = なし)。頂点の番号だけは符号なし (unsignedSmall)
	int32_t index(int size, bool unsignedSmall = false)
	{
		switch (size)
		{
		case 1: return unsignedSmall ? static_cast<int32_t>(get<uint8_t>()) : static_cast<int32_t>(get<int8_t>());
		case 2: return unsignedSmall ? static_cast<int32_t>(get<uint16_t>()) : static_cast<int32_t>(get<int16_t>());
		case 4: return get<int32_t>();
		default: ok = false; return -1;
		}
	}
	std::string bytes(size_t k)
	{
		if (!has(k)) { ok = false; return {}; }
		std::string s(reinterpret_cast<const char*>(p + pos), k);
		pos += k;
		return s;
	}
};

void appendUtf8(std::string& out, uint32_t c)
{
	if (c < 0x80) out += static_cast<char>(c);
	else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
	else if (c < 0x10000) { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
	else { out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
}

std::string utf16leToUtf8(const std::string& b)
{
	std::string out;
	for (size_t i = 0; i + 1 < b.size(); i += 2)
	{
		uint32_t c = static_cast<uint8_t>(b[i]) | (static_cast<uint8_t>(b[i + 1]) << 8);
		if (c >= 0xD800 && c <= 0xDBFF && i + 3 < b.size())
		{
			const uint32_t lo = static_cast<uint8_t>(b[i + 2]) | (static_cast<uint8_t>(b[i + 3]) << 8);
			if (lo >= 0xDC00 && lo <= 0xDFFF)
			{
				c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
				i += 2;
			}
		}
		appendUtf8(out, c);
	}
	return out;
}

// Shift_JIS (固定長の欄。0 で終わる) → UTF-8
std::string sjisToUtf8(std::string b)
{
	const size_t z = b.find('\0');
	if (z != std::string::npos) b.resize(z);
	if (b.empty()) return b;
#ifdef _WIN32
	const int wn = MultiByteToWideChar(932, 0, b.data(), static_cast<int>(b.size()), nullptr, 0);
	if (wn > 0)
	{
		std::wstring w(static_cast<size_t>(wn), L'\0');
		MultiByteToWideChar(932, 0, b.data(), static_cast<int>(b.size()), w.data(), wn);
		std::string out;
		for (size_t i = 0; i < w.size(); ++i)
		{
			uint32_t c = static_cast<uint16_t>(w[i]);
			if (c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size())
				c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<uint16_t>(w[++i]) - 0xDC00);
			appendUtf8(out, c);
		}
		return out;
	}
#endif
	for (char& c : b)
		if (static_cast<unsigned char>(c) >= 0x80) c = '?';
	return b;
}

std::string slashes(std::string s)
{
	std::replace(s.begin(), s.end(), '\\', '/');
	return s;
}

// ---- 座標の変換 (左手系 → 右手系: Z を反転) ----
struct Conv
{
	float s = k_MmdUnit;
	std::array<float, 3> pos(const std::array<float, 3>& v) const { return { v[0] * s, v[1] * s, -v[2] * s }; }
	std::array<float, 3> dir(const std::array<float, 3>& v) const { return { v[0], v[1], -v[2] }; }
	// 回転 (オイラー角、ラジアン) と回転の範囲: Z の鏡映で X / Y の回りが逆になる
	std::array<float, 3> rot(const std::array<float, 3>& v) const { return { -v[0], -v[1], v[2] }; }
	std::array<float, 4> quat(const std::array<float, 4>& q) const { return { -q[0], -q[1], q[2], q[3] }; }
	// 範囲 (min, max) の Z の反転: min.z ↔ -max.z
	void range(std::array<float, 3>& lo, std::array<float, 3>& hi, bool translation) const
	{
		if (translation)
		{
			for (int k = 0; k < 3; ++k) { lo[k] *= s; hi[k] *= s; }
			const float z0 = lo[2];
			lo[2] = -hi[2];
			hi[2] = -z0;
		}
		else
			for (int k = 0; k < 2; ++k)
			{
				const float a = lo[k];
				lo[k] = -hi[k];
				hi[k] = -a;
			}
	}
};

Json j3(const std::array<float, 3>& v) { return Json::array({ v[0], v[1], v[2] }); }
Json j4(const std::array<float, 4>& v) { return Json::array({ v[0], v[1], v[2], v[3] }); }

// ---- 共通: 骨の位置からノードとスキンを作る ----
struct BoneIn
{
	std::string          name;
	int32_t              parent = -1;
	std::array<float, 3> position {};   // モデルの座標 (変換後)
	Json                 extras;
};

void buildSkeleton(Model& out, const std::string& meshName, const std::vector<BoneIn>& bones)
{
	// ノード 0 = メッシュ。骨はノード 1 から
	ModelNode root;
	root.name = meshName.empty() ? "mesh" : meshName;
	root.mesh = 0;
	root.skin = bones.empty() ? -1 : 0;
	out.nodes.push_back(root);
	if (bones.empty())
		return;
	ModelSkin skin;
	skin.name = "mmd";
	for (size_t i = 0; i < bones.size(); ++i)
	{
		const BoneIn& b = bones[i];
		ModelNode n;
		n.name   = b.name;
		const bool hasParent = b.parent >= 0 && static_cast<size_t>(b.parent) < bones.size() && static_cast<size_t>(b.parent) != i;
		n.parent = hasParent ? b.parent + 1 : -1;
		const std::array<float, 3> base = hasParent ? bones[static_cast<size_t>(b.parent)].position : std::array<float, 3>{ 0, 0, 0 };
		n.translation = { b.position[0] - base[0], b.position[1] - base[1], b.position[2] - base[2] };
		n.extras = b.extras;
		out.nodes.push_back(std::move(n));
		skin.joints.push_back(static_cast<int32_t>(i + 1));
		// 逆バインド: 骨の空間 ← モデルの空間 = 骨の位置だけ戻す (回転はない)
		const float ib[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -b.position[0], -b.position[1], -b.position[2], 1 };
		skin.inverseBind.insert(skin.inverseBind.end(), ib, ib + 16);
	}
	out.skins.push_back(std::move(skin));
}

void finishModel(Model& out, ModelMesh& mesh, bool tangents)
{
	if (tangents)
		generateTangents(mesh);
	computeBounds(mesh);
	out.meshes.push_back(std::move(mesh));
	computeModelBounds(out);
}

float roughnessFromSpecular(float power)
{
	// ブリン・フォンの鋭さ → 粗さ (おおよそ)。MMD の反射は弱いので、粗めにする
	return std::clamp(std::sqrt(2.0f / (std::max(power, 0.0f) + 2.0f)), 0.3f, 1.0f);
}

// ============================================================
//  PMX
// ============================================================
bool readPmx(const std::string& data, Model& out, Diagnostics& diag, float scale, std::vector<std::string>* refs)
{
	Reader r { reinterpret_cast<const uint8_t*>(data.data()), data.size() };
	const Conv cv { scale };
	if (r.bytes(4) != "PMX ")
		return diag.error("PMX ではありません (先頭が 'PMX ' でない)"), false;
	const float version = r.get<float>();
	const uint8_t globalsCount = r.get<uint8_t>();
	std::vector<uint8_t> g(std::max<size_t>(globalsCount, 8), 0);
	for (size_t i = 0; i < globalsCount; ++i) g[i] = r.get<uint8_t>();
	if (!r.ok || globalsCount < 8)
		return diag.error("PMX のヘッダーが壊れています"), false;
	const bool utf8 = g[0] == 1;
	const int  addUv = g[1], vSize = g[2], tSize = g[3], mSize = g[4], bSize = g[5], morphSize = g[6], rSize = g[7];
	if (addUv > 4)
		return diag.error("PMX の追加 UV の数が不正です"), false;
	auto text = [&]() -> std::string {
		const int32_t len = r.get<int32_t>();
		if (len < 0) { r.ok = false; return {}; }
		const std::string b = r.bytes(static_cast<size_t>(len));
		return utf8 ? b : utf16leToUtf8(b);
	};

	Json mmd = { { "format", "pmx" }, { "version", version } };
	const std::string name = text(), nameEn = text(), comment = text(), commentEn = text();
	mmd["name"] = name; mmd["nameEn"] = nameEn; mmd["comment"] = comment; mmd["commentEn"] = commentEn;

	// ---- 頂点 ----
	const int32_t vcount = r.get<int32_t>();
	if (!r.ok || vcount < 0 || static_cast<size_t>(vcount) > data.size())
		return diag.error("PMX の頂点の数が不正です"), false;
	MeshBuilder mb(true, addUv >= 1, false, true);
	std::vector<std::array<float, 4>> extraUv;   // 追加 UV 1 (uv1 に使う xy 以外も extras には入れない)
	uint32_t sdefCount = 0, qdefCount = 0;
	for (int32_t i = 0; i < vcount && r.ok; ++i)
	{
		Vertex v;
		const auto p = cv.pos(r.f3());
		const auto nn = cv.dir(r.f3());
		std::copy(p.begin(), p.end(), v.pos);
		std::copy(nn.begin(), nn.end(), v.normal);
		v.uv0[0] = r.get<float>(); v.uv0[1] = r.get<float>();
		for (int k = 0; k < addUv; ++k)
		{
			const auto a = r.f4();
			if (k == 0) { v.uv1[0] = a[0]; v.uv1[1] = a[1]; }
		}
		std::vector<std::pair<uint32_t, float>> w;
		const uint8_t type = r.get<uint8_t>();
		auto bone = [&]() { const int32_t b = r.index(bSize); return b < 0 ? 0u : static_cast<uint32_t>(b); };
		switch (type)
		{
		case 0: w.push_back({ bone(), 1.0f }); break;
		case 1: case 3:
		{
			const uint32_t b0 = bone(), b1 = bone();
			const float    w0 = r.get<float>();
			w.push_back({ b0, w0 });
			w.push_back({ b1, 1.0f - w0 });
			if (type == 3) { r.skip(36); ++sdefCount; }   // SDEF の C / R0 / R1 は使わない (BDEF2 で近似)
			break;
		}
		case 2: case 4:
		{
			uint32_t b[4];
			for (auto& x : b) x = bone();
			for (int k = 0; k < 4; ++k) w.push_back({ b[k], r.get<float>() });
			if (type == 4) ++qdefCount;
			break;
		}
		default:
			return diag.error("PMX の頂点 " + std::to_string(i) + " の重みの種類が不正です (" + std::to_string(type) + ")"), false;
		}
		pickWeights(w, v.joints, v.weights);
		r.get<float>();   // 輪郭線の太さ
		mb.add(v, static_cast<uint32_t>(i), false);
	}
	if (sdefCount) diag.warn("SDEF の頂点 " + std::to_string(sdefCount) + " 個を BDEF2 で近似しました");
	if (qdefCount) diag.warn("QDEF の頂点 " + std::to_string(qdefCount) + " 個を BDEF4 で近似しました");

	// ---- 面 (三角形の向きを入れ替える) ----
	const int32_t icount = r.get<int32_t>();
	if (!r.ok || icount < 0 || icount % 3 != 0)
		return diag.error("PMX の面の数が不正です"), false;
	std::vector<uint32_t> faces(static_cast<size_t>(icount));
	for (auto& f : faces)
	{
		const int32_t idx = r.index(vSize, true);
		if (idx < 0 || idx >= vcount) { r.ok = false; break; }
		f = static_cast<uint32_t>(idx);
	}
	for (size_t i = 0; i + 2 < faces.size(); i += 3)
	{
		mb.index(faces[i]);
		mb.index(faces[i + 2]);
		mb.index(faces[i + 1]);
	}

	// ---- テクスチャ ----
	const int32_t tcount = r.get<int32_t>();
	std::vector<std::string> textures;
	for (int32_t i = 0; i < tcount && r.ok; ++i)
		textures.push_back(slashes(text()));
	auto tex = [&](int32_t i) { return i >= 0 && static_cast<size_t>(i) < textures.size() ? textures[static_cast<size_t>(i)] : std::string(); };

	// ---- 材質 ----
	ModelMesh mesh;
	const int32_t mcount = r.get<int32_t>();
	uint32_t offset = 0;
	for (int32_t i = 0; i < mcount && r.ok; ++i)
	{
		MaterialSlot s;
		s.name = text();
		const std::string nameEnM = text();
		const auto diffuse = r.f4();
		const auto specular = r.f3();
		const float specPower = r.get<float>();
		const auto ambient = r.f3();
		const uint8_t flags = r.get<uint8_t>();
		const auto edgeColor = r.f4();
		const float edgeSize = r.get<float>();
		const int32_t texIndex = r.index(tSize);
		const int32_t sphereIndex = r.index(tSize);
		const uint8_t sphereMode = r.get<uint8_t>();
		const uint8_t toonShared = r.get<uint8_t>();
		Json toon;
		if (toonShared == 0)
		{
			const std::string t = tex(r.index(tSize));
			if (!t.empty()) toon = { { "file", t } };
		}
		else
		{
			const uint8_t t = r.get<uint8_t>();
			char buf[16];
			std::snprintf(buf, sizeof(buf), "toon%02d.bmp", t + 1);
			toon = { { "shared", t }, { "file", buf } };
		}
		const std::string memo = text();
		const int32_t surfaceCount = r.get<int32_t>();
		if (!r.ok || surfaceCount < 0 || offset + static_cast<uint32_t>(surfaceCount) > static_cast<uint32_t>(icount))
			return diag.error("PMX の材質 " + std::to_string(i) + " の面の数が不正です"), false;

		s.baseColor   = { diffuse[0], diffuse[1], diffuse[2], diffuse[3] };
		s.metallic    = 0;
		s.roughness   = roughnessFromSpecular(specPower);
		s.doubleSided = (flags & 0x01) != 0;
		s.alphaMode   = diffuse[3] < 0.999f ? AlphaMode::Blend : AlphaMode::Opaque;
		s.baseColorTexture.path = tex(texIndex);
		Json m = {
			{ "nameEn", nameEnM }, { "specular", j3(specular) }, { "specularPower", specPower }, { "ambient", j3(ambient) },
			{ "flags", flags }, { "castShadow", (flags & 0x04) != 0 }, { "receiveShadow", (flags & 0x08) != 0 },
			{ "groundShadow", (flags & 0x02) != 0 },
			{ "edge", { { "enabled", (flags & 0x10) != 0 }, { "color", j4(edgeColor) }, { "size", edgeSize } } },
			{ "toon", toon }, { "memo", memo },
		};
		if (!tex(sphereIndex).empty() && sphereMode != 0)
			m["sphere"] = { { "file", tex(sphereIndex) }, { "mode", sphereMode == 1 ? "multiply" : sphereMode == 2 ? "add" : "subTexture" } };
		s.extras = { { "mmd", m } };
		out.slots.push_back(std::move(s));
		mesh.submeshes.push_back({ offset, static_cast<uint32_t>(surfaceCount), static_cast<uint32_t>(i) });
		offset += static_cast<uint32_t>(surfaceCount);
		if (refs)
		{
			for (const std::string& f : { tex(texIndex), tex(sphereIndex), toonShared == 0 ? std::string(toon.value("file", "")) : std::string() })
				if (!f.empty() && std::find(refs->begin(), refs->end(), f) == refs->end())
					refs->push_back(f);
		}
	}
	if (!r.ok)
		return diag.error("PMX の材質が途中で切れています"), false;

	// ---- 骨 ----
	const int32_t bcount = r.get<int32_t>();
	std::vector<BoneIn> bones;
	auto node = [](int32_t bone) { return bone < 0 ? -1 : bone + 1; };
	for (int32_t i = 0; i < bcount && r.ok; ++i)
	{
		BoneIn b;
		b.name = text();
		const std::string nameEnB = text();
		b.position = cv.pos(r.f3());
		b.parent = r.index(bSize);
		const int32_t layer = r.get<int32_t>();
		const uint16_t flags = r.get<uint16_t>();
		Json m = { { "nameEn", nameEnB }, { "layer", layer }, { "flags", flags },
		           { "rotatable", (flags & 0x0002) != 0 }, { "translatable", (flags & 0x0004) != 0 },
		           { "visible", (flags & 0x0008) != 0 }, { "enabled", (flags & 0x0010) != 0 },
		           { "physicsAfterDeform", (flags & 0x1000) != 0 } };
		if (flags & 0x0001) m["tailBone"] = node(r.index(bSize));
		else                m["tailOffset"] = j3(cv.pos(r.f3()));
		if (flags & (0x0100 | 0x0200))
		{
			const int32_t parent = r.index(bSize);
			const float   weight = r.get<float>();
			m["inherit"] = { { "parent", node(parent) }, { "weight", weight },
			                 { "rotation", (flags & 0x0100) != 0 }, { "translation", (flags & 0x0200) != 0 } };
		}
		if (flags & 0x0400) m["fixedAxis"] = j3(cv.dir(r.f3()));
		if (flags & 0x0800)
		{
			const auto x = cv.dir(r.f3()), z = cv.dir(r.f3());
			m["localAxes"] = { { "x", j3(x) }, { "z", j3(z) } };
		}
		if (flags & 0x2000) m["externalParent"] = r.get<int32_t>();
		if (flags & 0x0020)
		{
			Json ik = { { "target", node(r.index(bSize)) }, { "loop", r.get<int32_t>() }, { "limitAngle", r.get<float>() } };
			const int32_t links = r.get<int32_t>();
			Json jl = Json::array();
			for (int32_t k = 0; k < links && r.ok; ++k)
			{
				Json l = { { "bone", node(r.index(bSize)) } };
				if (r.get<uint8_t>() == 1)
				{
					auto lo = r.f3(), hi = r.f3();
					cv.range(lo, hi, false);
					l["min"] = j3(lo);
					l["max"] = j3(hi);
				}
				jl.push_back(std::move(l));
			}
			ik["links"] = std::move(jl);
			m["ik"] = std::move(ik);
		}
		b.extras = { { "mmd", m } };
		bones.push_back(std::move(b));
	}
	if (!r.ok)
		return diag.error("PMX の骨が途中で切れています"), false;

	// ---- モーフ ----
	const int32_t morphCount = r.get<int32_t>();
	Json otherMorphs = Json::array();
	static const char* kTypes[] = { "group", "vertex", "bone", "uv", "uv1", "uv2", "uv3", "uv4", "material", "flip", "impulse" };
	for (int32_t i = 0; i < morphCount && r.ok; ++i)
	{
		const std::string mname = text(), mnameEn = text();
		const uint8_t panel = r.get<uint8_t>();
		const uint8_t type  = r.get<uint8_t>();
		const int32_t count = r.get<int32_t>();
		if (!r.ok || type > 10 || count < 0)
			return diag.error("PMX のモーフ " + std::to_string(i) + " が不正です"), false;
		if (type == 1)
		{
			MorphTarget mt;
			mt.name = mname;
			mt.positions.assign(static_cast<size_t>(vcount) * 3, 0.0f);
			for (int32_t k = 0; k < count && r.ok; ++k)
			{
				const int32_t vi = r.index(vSize, true);
				const auto d = cv.pos(r.f3());
				if (vi >= 0 && vi < vcount)
					for (int c = 0; c < 3; ++c) mt.positions[static_cast<size_t>(vi) * 3 + c] = d[static_cast<size_t>(c)];
			}
			mesh.morphs.push_back(std::move(mt));
			continue;
		}
		Json om = { { "name", mname }, { "nameEn", mnameEn }, { "panel", panel }, { "type", kTypes[type] } };
		Json offs = Json::array();
		for (int32_t k = 0; k < count && r.ok; ++k)
		{
			switch (type)
			{
			case 0: case 9: offs.push_back({ { "morph", r.index(morphSize) }, { "weight", r.get<float>() } }); break;
			case 2:
			{
				const int32_t b = r.index(bSize);
				const auto t = cv.pos(r.f3());
				const auto q = cv.quat(r.f4());
				offs.push_back({ { "bone", node(b) }, { "translation", j3(t) }, { "rotation", j4(q) } });
				break;
			}
			case 3: case 4: case 5: case 6: case 7:
			{
				const int32_t vi = r.index(vSize, true);
				offs.push_back({ { "vertex", vi }, { "value", j4(r.f4()) } });
				break;
			}
			case 8:
			{
				Json mo = { { "material", r.index(mSize) }, { "op", r.get<uint8_t>() == 0 ? "multiply" : "add" } };
				mo["diffuse"] = j4(r.f4()); mo["specular"] = j3(r.f3()); mo["specularPower"] = r.get<float>();
				mo["ambient"] = j3(r.f3()); mo["edgeColor"] = j4(r.f4()); mo["edgeSize"] = r.get<float>();
				mo["texture"] = j4(r.f4()); mo["sphere"] = j4(r.f4()); mo["toon"] = j4(r.f4());
				offs.push_back(std::move(mo));
				break;
			}
			case 10:
			{
				Json io = { { "rigidBody", r.index(rSize) }, { "local", r.get<uint8_t>() != 0 } };
				io["velocity"] = j3(cv.dir(r.f3()));
				io["torque"] = j3(cv.rot(r.f3()));
				offs.push_back(std::move(io));
				break;
			}
			default: break;
			}
		}
		om["offsets"] = std::move(offs);
		otherMorphs.push_back(std::move(om));
	}
	if (!r.ok)
		return diag.error("PMX のモーフが途中で切れています"), false;
	mmd["morphs"] = std::move(otherMorphs);

	// ---- 表示枠 / 剛体 / ジョイント (保存だけ。途中で切れていたら警告して、ここまでで使う) ----
	auto optional = [&]() -> bool {
		const int32_t frames = r.get<int32_t>();
		Json jf = Json::array();
		for (int32_t i = 0; i < frames && r.ok; ++i)
		{
			Json f = { { "name", text() }, { "nameEn", text() }, { "special", r.get<uint8_t>() != 0 } };
			const int32_t n = r.get<int32_t>();
			Json items = Json::array();
			for (int32_t k = 0; k < n && r.ok; ++k)
			{
				const uint8_t t = r.get<uint8_t>();
				items.push_back(t == 0 ? Json{ { "bone", node(r.index(bSize)) } } : Json{ { "morph", r.index(morphSize) } });
			}
			f["items"] = std::move(items);
			jf.push_back(std::move(f));
		}
		if (!r.ok) return false;
		mmd["displayFrames"] = std::move(jf);

		const int32_t bodies = r.get<int32_t>();
		Json jb = Json::array();
		for (int32_t i = 0; i < bodies && r.ok; ++i)
		{
			Json b = { { "name", text() }, { "nameEn", text() } };
			b["bone"] = node(r.index(bSize));
			b["group"] = r.get<uint8_t>();
			b["noCollision"] = r.get<uint16_t>();
			const uint8_t shape = r.get<uint8_t>();
			b["shape"] = shape == 0 ? "sphere" : shape == 1 ? "box" : "capsule";
			auto size = r.f3();
			for (float& x : size) x *= scale;
			b["size"] = j3(size);
			b["position"] = j3(cv.pos(r.f3()));
			b["rotation"] = j3(cv.rot(r.f3()));
			b["mass"] = r.get<float>(); b["linearDamping"] = r.get<float>(); b["angularDamping"] = r.get<float>();
			b["restitution"] = r.get<float>(); b["friction"] = r.get<float>();
			const uint8_t mode = r.get<uint8_t>();
			b["mode"] = mode == 0 ? "static" : mode == 1 ? "dynamic" : "dynamicWithBone";
			jb.push_back(std::move(b));
		}
		if (!r.ok) return false;
		mmd["rigidBodies"] = std::move(jb);

		const int32_t joints = r.get<int32_t>();
		Json jj = Json::array();
		for (int32_t i = 0; i < joints && r.ok; ++i)
		{
			Json j = { { "name", text() }, { "nameEn", text() }, { "type", r.get<uint8_t>() } };
			j["bodyA"] = r.index(rSize);
			j["bodyB"] = r.index(rSize);
			j["position"] = j3(cv.pos(r.f3()));
			j["rotation"] = j3(cv.rot(r.f3()));
			auto pl = r.f3(), ph = r.f3(), rl = r.f3(), rh = r.f3();
			cv.range(pl, ph, true);
			cv.range(rl, rh, false);
			j["translationMin"] = j3(pl); j["translationMax"] = j3(ph);
			j["rotationMin"] = j3(rl); j["rotationMax"] = j3(rh);
			auto sp = r.f3();
			for (float& x : sp) x *= scale;
			j["springTranslation"] = j3(sp);
			j["springRotation"] = j3(r.f3());
			jj.push_back(std::move(j));
		}
		if (!r.ok) return false;
		mmd["joints"] = std::move(jj);
		return true;
	};
	if (r.pos < r.n && !optional())
		diag.warn("PMX の表示枠 / 剛体 / ジョイントが途中で切れています (読めたところまで使います)");

	// ---- まとめる ----
	mb.finish(mesh);
	mesh.name = name.empty() ? "mmd" : name;
	buildSkeleton(out, mesh.name, bones);
	out.extras = { { "mmd", std::move(mmd) } };
	finishModel(out, mesh, true);
	return true;
}

// ============================================================
//  PMD
// ============================================================
bool readPmd(const std::string& data, Model& out, Diagnostics& diag, float scale, std::vector<std::string>* refs)
{
	Reader r { reinterpret_cast<const uint8_t*>(data.data()), data.size() };
	const Conv cv { scale };
	if (r.bytes(3) != "Pmd")
		return diag.error("PMD ではありません (先頭が 'Pmd' でない)"), false;
	const float version = r.get<float>();
	const std::string name = sjisToUtf8(r.bytes(20)), comment = sjisToUtf8(r.bytes(256));
	Json mmd = { { "format", "pmd" }, { "version", version }, { "name", name }, { "comment", comment } };

	const uint32_t vcount = r.get<uint32_t>();
	if (!r.ok || static_cast<size_t>(vcount) * 38 > data.size())
		return diag.error("PMD の頂点の数が不正です"), false;
	MeshBuilder mb(true, false, false, true);
	for (uint32_t i = 0; i < vcount && r.ok; ++i)
	{
		Vertex v;
		const auto p = cv.pos(r.f3());
		const auto nn = cv.dir(r.f3());
		std::copy(p.begin(), p.end(), v.pos);
		std::copy(nn.begin(), nn.end(), v.normal);
		v.uv0[0] = r.get<float>(); v.uv0[1] = r.get<float>();
		const uint16_t b0 = r.get<uint16_t>(), b1 = r.get<uint16_t>();
		const float    w0 = r.get<uint8_t>() / 100.0f;
		r.get<uint8_t>();   // 輪郭線を描かない印
		std::vector<std::pair<uint32_t, float>> w = { { b0, w0 }, { b1, 1.0f - w0 } };
		pickWeights(w, v.joints, v.weights);
		mb.add(v, i, false);
	}
	const uint32_t icount = r.get<uint32_t>();
	if (!r.ok || icount % 3 != 0 || static_cast<size_t>(icount) * 2 > data.size())
		return diag.error("PMD の面の数が不正です"), false;
	std::vector<uint32_t> faces(icount);
	for (auto& f : faces)
	{
		f = r.get<uint16_t>();
		if (f >= vcount) r.ok = false;
	}
	if (!r.ok)
		return diag.error("PMD の面が不正です"), false;
	for (size_t i = 0; i + 2 < faces.size(); i += 3)
	{
		mb.index(faces[i]);
		mb.index(faces[i + 2]);
		mb.index(faces[i + 1]);
	}

	ModelMesh mesh;
	const uint32_t mcount = r.get<uint32_t>();
	uint32_t offset = 0;
	std::vector<uint8_t> toonIndex;
	for (uint32_t i = 0; i < mcount && r.ok; ++i)
	{
		MaterialSlot s;
		const auto diffuse = r.f3();
		const float alpha = r.get<float>();
		const float specPower = r.get<float>();
		const auto specular = r.f3();
		const auto ambient = r.f3();
		const uint8_t toon = r.get<uint8_t>();
		const uint8_t edge = r.get<uint8_t>();
		const uint32_t faceCount = r.get<uint32_t>();
		const std::string texField = slashes(sjisToUtf8(r.bytes(20)));
		if (!r.ok || offset + faceCount > icount)
			return diag.error("PMD の材質 " + std::to_string(i) + " の面の数が不正です"), false;
		// "tex.bmp*sphere.sph" の形 (スフィアだけのこともある)
		std::string texFile, sphereFile;
		for (size_t start = 0; start <= texField.size();)
		{
			const size_t star = texField.find('*', start);
			const std::string part = texField.substr(start, star == std::string::npos ? std::string::npos : star - start);
			std::string lower = part;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (!part.empty())
				(lower.ends_with(".sph") || lower.ends_with(".spa") ? sphereFile : texFile) = part;
			if (star == std::string::npos) break;
			start = star + 1;
		}
		char nameBuf[32];
		std::snprintf(nameBuf, sizeof(nameBuf), "material%u", i);
		s.name        = nameBuf;
		s.baseColor   = { diffuse[0], diffuse[1], diffuse[2], alpha };
		s.roughness   = roughnessFromSpecular(specPower);
		s.doubleSided = alpha < 0.999f;   // PMD は両面の印がない (半透明は両面で描くのが MMD の決まり)
		s.alphaMode   = alpha < 0.999f ? AlphaMode::Blend : AlphaMode::Opaque;
		s.baseColorTexture.path = texFile;
		Json m = { { "specular", j3(specular) }, { "specularPower", specPower }, { "ambient", j3(ambient) },
		           { "edge", { { "enabled", edge != 0 } } } };
		if (!sphereFile.empty())
		{
			std::string lower = sphereFile;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			m["sphere"] = { { "file", sphereFile }, { "mode", lower.ends_with(".spa") ? "add" : "multiply" } };
		}
		toonIndex.push_back(toon);
		s.extras = { { "mmd", m } };
		out.slots.push_back(std::move(s));
		mesh.submeshes.push_back({ offset, faceCount, i });
		offset += faceCount;
		if (refs)
			for (const std::string& f : { texFile, sphereFile })
				if (!f.empty() && std::find(refs->begin(), refs->end(), f) == refs->end())
					refs->push_back(f);
	}
	if (!r.ok)
		return diag.error("PMD の材質が途中で切れています"), false;

	// ---- 骨 ----
	const uint16_t bcount = r.get<uint16_t>();
	std::vector<BoneIn> bones;
	auto node = [](int32_t bone) { return bone < 0 || bone == 0xFFFF ? -1 : bone + 1; };
	for (uint16_t i = 0; i < bcount && r.ok; ++i)
	{
		BoneIn b;
		b.name = sjisToUtf8(r.bytes(20));
		const uint16_t parent = r.get<uint16_t>();
		const uint16_t tail   = r.get<uint16_t>();
		const uint8_t  type   = r.get<uint8_t>();
		const uint16_t ikParent = r.get<uint16_t>();
		b.position = cv.pos(r.f3());
		b.parent = parent == 0xFFFF ? -1 : parent;
		b.extras = { { "mmd", { { "type", type }, { "tailBone", node(tail == 0 ? -1 : tail) }, { "ikBone", node(ikParent == 0 ? -1 : ikParent) } } } };
		bones.push_back(std::move(b));
	}
	// ---- IK ----
	const uint16_t ikCount = r.get<uint16_t>();
	Json iks = Json::array();
	for (uint16_t i = 0; i < ikCount && r.ok; ++i)
	{
		const uint16_t ikBone = r.get<uint16_t>(), target = r.get<uint16_t>();
		const uint8_t  chain = r.get<uint8_t>();
		const uint16_t iterations = r.get<uint16_t>();
		const float    limit = r.get<float>();
		Json links = Json::array();
		for (uint8_t k = 0; k < chain && r.ok; ++k)
			links.push_back({ { "bone", node(r.get<uint16_t>()) } });
		iks.push_back({ { "bone", node(ikBone) }, { "target", node(target) }, { "loop", iterations }, { "limitAngle", limit * 4.0f },
		                { "links", std::move(links) } });
	}
	mmd["ik"] = std::move(iks);
	if (!r.ok)
		return diag.error("PMD の骨 / IK が途中で切れています"), false;

	// ---- 表情 (頂点モーフ。0 番は base: 絶対位置の頂点の番号の表) ----
	const uint16_t skinCount = r.get<uint16_t>();
	std::vector<uint32_t> baseIndices;
	std::vector<std::string> morphNames;
	for (uint16_t i = 0; i < skinCount && r.ok; ++i)
	{
		const std::string sname = sjisToUtf8(r.bytes(20));
		const uint32_t count = r.get<uint32_t>();
		const uint8_t  type  = r.get<uint8_t>();
		if (!r.ok || static_cast<size_t>(count) * 16 > data.size() - r.pos)
			return diag.error("PMD の表情 " + std::to_string(i) + " が不正です"), false;
		if (type == 0 && baseIndices.empty())
		{
			for (uint32_t k = 0; k < count; ++k)
			{
				baseIndices.push_back(r.get<uint32_t>());
				r.f3();
			}
			continue;
		}
		MorphTarget mt;
		mt.name = sname;
		mt.positions.assign(static_cast<size_t>(vcount) * 3, 0.0f);
		for (uint32_t k = 0; k < count && r.ok; ++k)
		{
			const uint32_t bi = r.get<uint32_t>();
			const auto d = cv.pos(r.f3());
			if (bi < baseIndices.size() && baseIndices[bi] < vcount)
				for (int c = 0; c < 3; ++c) mt.positions[static_cast<size_t>(baseIndices[bi]) * 3 + c] = d[static_cast<size_t>(c)];
		}
		morphNames.push_back(mt.name);
		mesh.morphs.push_back(std::move(mt));
	}
	if (!r.ok)
		return diag.error("PMD の表情が途中で切れています"), false;

	// ---- 以降は省略できる部分 (表示枠、英語名、トゥーン、剛体、ジョイント)。切れていたらそこまで ----
	auto optional = [&]() -> bool {
		const uint8_t skinDisp = r.get<uint8_t>();
		r.skip(static_cast<size_t>(skinDisp) * 2);
		const uint8_t frameNames = r.get<uint8_t>();
		r.skip(static_cast<size_t>(frameNames) * 50);
		const uint32_t boneDisp = r.get<uint32_t>();
		r.skip(static_cast<size_t>(boneDisp) * 3);
		if (!r.ok) return false;
		if (r.pos >= r.n) return true;
		if (r.get<uint8_t>() == 1)   // 英語名
		{
			mmd["nameEn"] = sjisToUtf8(r.bytes(20));
			mmd["commentEn"] = sjisToUtf8(r.bytes(256));
			for (uint16_t i = 0; i < bcount && r.ok; ++i)
			{
				const std::string en = sjisToUtf8(r.bytes(20));
				if (i < bones.size()) bones[i].extras["mmd"]["nameEn"] = en;
			}
			r.skip(static_cast<size_t>(skinCount > 0 ? skinCount - 1 : 0) * 20);
			r.skip(static_cast<size_t>(frameNames) * 50);
		}
		if (!r.ok) return false;
		if (r.pos >= r.n) return true;
		Json toons = Json::array();
		for (int i = 0; i < 10 && r.ok; ++i)
			toons.push_back(slashes(sjisToUtf8(r.bytes(100))));
		if (!r.ok) return false;
		mmd["toonTextures"] = toons;
		if (r.pos >= r.n) return true;
		const uint32_t bodies = r.get<uint32_t>();
		Json jb = Json::array();
		for (uint32_t i = 0; i < bodies && r.ok; ++i)
		{
			Json b = { { "name", sjisToUtf8(r.bytes(20)) } };
			const uint16_t bone = r.get<uint16_t>();
			b["bone"] = node(bone);
			b["group"] = r.get<uint8_t>();
			b["noCollision"] = r.get<uint16_t>();
			const uint8_t shape = r.get<uint8_t>();
			b["shape"] = shape == 0 ? "sphere" : shape == 1 ? "box" : "capsule";
			auto size = r.f3();
			for (float& x : size) x *= scale;
			b["size"] = j3(size);
			b["positionFromBone"] = j3(cv.pos(r.f3()));   // PMD は骨からの相対
			b["rotation"] = j3(cv.rot(r.f3()));
			b["mass"] = r.get<float>(); b["linearDamping"] = r.get<float>(); b["angularDamping"] = r.get<float>();
			b["restitution"] = r.get<float>(); b["friction"] = r.get<float>();
			const uint8_t mode = r.get<uint8_t>();
			b["mode"] = mode == 0 ? "static" : mode == 1 ? "dynamic" : "dynamicWithBone";
			jb.push_back(std::move(b));
		}
		if (!r.ok) return false;
		mmd["rigidBodies"] = std::move(jb);
		const uint32_t joints = r.get<uint32_t>();
		Json jj = Json::array();
		for (uint32_t i = 0; i < joints && r.ok; ++i)
		{
			Json j = { { "name", sjisToUtf8(r.bytes(20)) } };
			j["bodyA"] = r.get<uint32_t>();
			j["bodyB"] = r.get<uint32_t>();
			j["position"] = j3(cv.pos(r.f3()));
			j["rotation"] = j3(cv.rot(r.f3()));
			auto pl = r.f3(), ph = r.f3(), rl = r.f3(), rh = r.f3();
			cv.range(pl, ph, true);
			cv.range(rl, rh, false);
			j["translationMin"] = j3(pl); j["translationMax"] = j3(ph);
			j["rotationMin"] = j3(rl); j["rotationMax"] = j3(rh);
			auto sp = r.f3();
			for (float& x : sp) x *= scale;
			j["springTranslation"] = j3(sp);
			j["springRotation"] = j3(r.f3());
			jj.push_back(std::move(j));
		}
		if (!r.ok) return false;
		mmd["joints"] = std::move(jj);
		return true;
	};
	if (r.pos < r.n && !optional())
		diag.warn("PMD の拡張 (英語名 / トゥーン / 剛体 / ジョイント) が途中で切れています (読めたところまで使います)");

	// トゥーン: 材質の番号 → ファイル名 (表がなければ MMD の共有の toon01〜10.bmp)
	const Json toons = mmd.value("toonTextures", Json::array());
	for (size_t i = 0; i < out.slots.size() && i < toonIndex.size(); ++i)
	{
		const uint8_t t = toonIndex[i];
		if (t == 0xFF) continue;
		char shared[16];
		std::snprintf(shared, sizeof(shared), "toon%02d.bmp", t + 1);
		std::string file = t < toons.size() ? toons[t].get<std::string>() : std::string();
		if (file.empty()) file = shared;
		out.slots[i].extras["mmd"]["toon"] = { { "file", file }, { "shared", t } };
	}

	mb.finish(mesh);
	mesh.name = name.empty() ? "mmd" : name;
	buildSkeleton(out, mesh.name, bones);
	out.extras = { { "mmd", std::move(mmd) } };
	finishModel(out, mesh, true);
	return true;
}

bool isPmd(const std::filesystem::path& p)
{
	std::string ext = genericUtf8(p.extension());
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext == ".pmd";
}

bool readMmd(const std::filesystem::path& file, Model& out, Diagnostics& diag, float scale, std::vector<std::string>* refs)
{
	std::string data;
	if (!readFile(file, data))
		return diag.error("読めません: " + pathToUtf8(file)), false;
	return isPmd(file) ? readPmd(data, out, diag, scale, refs) : readPmx(data, out, diag, scale, refs);
}
} // namespace

bool importMmd(const std::filesystem::path& file, Model& out, Diagnostics& diag, float scale)
{
	return readMmd(file, out, diag, k_MmdUnit * scale, nullptr);
}

std::vector<std::string> mmdReferencedFiles(const std::filesystem::path& file, Diagnostics& diag)
{
	Model m;
	std::vector<std::string> refs;
	readMmd(file, m, diag, k_MmdUnit, &refs);
	return refs;
}
} // namespace sunset::asset::detail
