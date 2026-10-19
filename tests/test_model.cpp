// ============================================================
//  モデルの取り込みの単体テスト: glTF (テストの中で書く)、FBX (手書きの ASCII)、OBJ
// ============================================================
#include <doctest/doctest.h>
#include "sunset/asset/AssetDatabase.h"
#include "sunset/asset/model/ModelImporter.h"

#include <cmath>
#include <cstring>
#include <fstream>

using namespace sunset::asset;
namespace fs = std::filesystem;

namespace
{
fs::path freshDir(const char* name)
{
	const fs::path d = fs::temp_directory_path() / name;
	std::error_code ec;
	fs::remove_all(d, ec);
	fs::create_directories(d);
	return d;
}

void writeBytes(const fs::path& p, const std::string& bytes)
{
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// glTF のバッファを作る: 配列を足して、bufferView と accessor の JSON を返す
struct GltfBuilder
{
	std::string bin;
	Json        views = Json::array(), accessors = Json::array();

	template<typename T>
	int add(const std::vector<T>& data, const char* type, int componentType, size_t count, bool minmax = false)
	{
		while (bin.size() % 4) bin.push_back('\0');
		const size_t offset = bin.size();
		bin.append(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(T));
		views.push_back({ { "buffer", 0 }, { "byteOffset", offset }, { "byteLength", data.size() * sizeof(T) } });
		Json acc = { { "bufferView", views.size() - 1 }, { "componentType", componentType }, { "count", count }, { "type", type } };
		if (minmax)   // POSITION は min / max が必須
		{
			Json lo = Json::array({ 1e9, 1e9, 1e9 }), hi = Json::array({ -1e9, -1e9, -1e9 });
			for (size_t i = 0; i < count; ++i)
				for (size_t k = 0; k < 3; ++k)
				{
					const double v = static_cast<double>(data[i * 3 + k]);
					lo[k] = std::min(lo[k].get<double>(), v);
					hi[k] = std::max(hi[k].get<double>(), v);
				}
			acc["min"] = lo;
			acc["max"] = hi;
		}
		accessors.push_back(acc);
		return static_cast<int>(accessors.size() - 1);
	}
};

constexpr int FLOAT = 5126, USHORT = 5123, UINT = 5125;

// 四角形 (2 つの三角形、マテリアル 0、モーフ 1 つ) + 三角形 (マテリアル 1)。スキンの骨は 2 本
void writeTestGltf(const fs::path& dir)
{
	GltfBuilder b;
	const std::vector<float> qpos = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
	const std::vector<float> qnor = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
	const std::vector<float> quv  = { 0, 1, 1, 1, 1, 0, 0, 0 };
	const std::vector<uint16_t> qjnt = { 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
	const std::vector<float> qwgt = { 0.5f, 0.5f, 0, 0, 0.25f, 0.25f, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0 };   // 最後は合計 2 (正規化される)
	const std::vector<uint32_t> qidx = { 0, 1, 2, 0, 2, 3 };
	const std::vector<float> qmorph = { 0, 0, 0.5f, 0, 0, 0.5f, 0, 0, 0, 0, 0, 0 };
	const std::vector<float> tpos = { 2, 0, 0, 3, 0, 0, 2, 1, 0 };
	const std::vector<float> tnor = { 0, 0, 1, 0, 0, 1, 0, 0, 1 };
	const std::vector<uint16_t> tjnt = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	const std::vector<float> twgt = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
	const std::vector<float> ibm = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,   1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -1, 0, 0, 1 };
	const int aP = b.add(qpos, "VEC3", FLOAT, 4, true), aN = b.add(qnor, "VEC3", FLOAT, 4), aU = b.add(quv, "VEC2", FLOAT, 4);
	const int aJ = b.add(qjnt, "VEC4", USHORT, 4), aW = b.add(qwgt, "VEC4", FLOAT, 4), aI = b.add(qidx, "SCALAR", UINT, 6);
	const int aM = b.add(qmorph, "VEC3", FLOAT, 4, true);
	const int tP = b.add(tpos, "VEC3", FLOAT, 3, true), tN = b.add(tnor, "VEC3", FLOAT, 3), tJ = b.add(tjnt, "VEC4", USHORT, 3), tW = b.add(twgt, "VEC4", FLOAT, 3);
	const int aIbm = b.add(ibm, "MAT4", FLOAT, 2);
	const int tM = b.add(std::vector<float>(9, 0.0f), "VEC3", FLOAT, 3, true);   // glTF ではすべての primitive が同じ数のモーフを持つ
	const Json gltf = {
		{ "asset", { { "version", "2.0" } } },
		{ "buffers", Json::array({ { { "uri", "test%20data.bin" }, { "byteLength", b.bin.size() } } }) },
		{ "bufferViews", b.views },
		{ "accessors", b.accessors },
		{ "images", Json::array({ { { "uri", "textures/albedo.png" } } }) },
		{ "textures", Json::array({ { { "source", 0 } } }) },
		{ "materials", Json::array({
			{ { "name", "Skin" }, { "pbrMetallicRoughness", { { "baseColorFactor", { 1, 0.5, 0.25, 1 } }, { "metallicFactor", 0.0 },
			  { "roughnessFactor", 0.8 }, { "baseColorTexture", { { "index", 0 } } } } } },
			{ { "name", "Cloth" }, { "alphaMode", "MASK" }, { "doubleSided", true } },
		}) },
		{ "meshes", Json::array({ {
			{ "name", "Body" },
			{ "primitives", Json::array({
				{ { "attributes", { { "POSITION", aP }, { "NORMAL", aN }, { "TEXCOORD_0", aU }, { "JOINTS_0", aJ }, { "WEIGHTS_0", aW } } },
				  { "indices", aI }, { "material", 0 }, { "targets", Json::array({ { { "POSITION", aM } } }) } },
				{ { "attributes", { { "POSITION", tP }, { "NORMAL", tN }, { "JOINTS_0", tJ }, { "WEIGHTS_0", tW } } }, { "material", 1 },
				  { "targets", Json::array({ { { "POSITION", tM } } }) } },
			}) },
			{ "weights", { 0.25 } },
			{ "extras", { { "targetNames", { "Smile" } } } },
		} }) },
		{ "skins", Json::array({ { { "joints", { 2, 3 } }, { "inverseBindMatrices", aIbm } } }) },
		{ "nodes", Json::array({
			{ { "name", "Root" }, { "translation", { 0, 0, -5 } }, { "children", { 1, 2 } } },
			{ { "name", "BodyMesh" }, { "mesh", 0 }, { "skin", 0 } },
			{ { "name", "Hips" }, { "children", { 3 } } },
			{ { "name", "Spine" }, { "translation", { 1, 0, 0 } }, { "rotation", { 0, 0.7071068, 0, 0.7071068 } } },
		}) },
		{ "scenes", Json::array({ { { "nodes", { 0 } } } }) },
		{ "scene", 0 },
	};
	writeBytes(dir / "test data.bin", b.bin);
	writeBytes(dir / "hero.gltf", gltf.dump(1));
}

// 手書きの ASCII FBX: Y が上、単位は cm (UnitScaleFactor 1)。100 cm の四角形と赤いマテリアル
const char* k_Fbx = R"fbx(; FBX 7.4.0 project file
FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7400
}
GlobalSettings:  {
	Version: 1000
	Properties70:  {
		P: "UpAxis", "int", "Integer", "",1
		P: "UpAxisSign", "int", "Integer", "",1
		P: "FrontAxis", "int", "Integer", "",2
		P: "FrontAxisSign", "int", "Integer", "",1
		P: "CoordAxis", "int", "Integer", "",0
		P: "CoordAxisSign", "int", "Integer", "",1
		P: "UnitScaleFactor", "double", "Number", "",1
	}
}
Objects:  {
	Geometry: 1000, "Geometry::Quad", "Mesh" {
		Vertices: *12 {
			a: 0,0,0,100,0,0,100,100,0,0,100,0
		}
		PolygonVertexIndex: *4 {
			a: 0,1,2,-4
		}
		LayerElementMaterial: 0 {
			Version: 101
			Name: ""
			MappingInformationType: "AllSame"
			ReferenceInformationType: "IndexToDirect"
			Materials: *1 {
				a: 0
			}
		}
		Layer: 0 {
			Version: 100
			LayerElement:  {
				Type: "LayerElementMaterial"
				TypedIndex: 0
			}
		}
	}
	Model: 2000, "Model::Quad", "Mesh" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",200,0,0
		}
	}
	Material: 3000, "Material::Red", "" {
		Version: 102
		ShadingModel: "phong"
		Properties70:  {
			P: "DiffuseColor", "Color", "", "A",1,0,0
		}
	}
}
Connections:  {
	C: "OO",2000,0
	C: "OO",1000,2000
	C: "OO",3000,2000
}
)fbx";

struct Fixture
{
	ImporterRegistry reg;
	AssetDatabase    db { reg };
	Fixture() { reg.add(makeModelImporter()); }
};

float sumWeights(const ModelMesh& m, size_t v) { return m.weights[v * 4] + m.weights[v * 4 + 1] + m.weights[v * 4 + 2] + m.weights[v * 4 + 3]; }
} // namespace

TEST_CASE("モデル: glTF を取り込む (ノード、サブメッシュ、スロット、スキン、モーフ)")
{
	const fs::path root = freshDir("sasset_model_gltf"), cache = freshDir("sasset_model_gltf_cache");
	writeTestGltf(root);
	Fixture f;
	Diagnostics diag;
	f.db.scan(root, diag);
	const AssetRecord* r = f.db.findByPath("hero.gltf");
	REQUIRE(r);
	CHECK(r->meta.importer == "model");

	// 参照しているファイル (プロジェクトにコピーするもの)
	const auto refs = r->importer->referencedFiles(r->source, diag);
	CHECK(refs == std::vector<std::string>{ "test data.bin", "textures/albedo.png" });

	fs::path artifact;
	REQUIRE_MESSAGE(f.db.import(*r, cache, artifact, diag), (diag.errors.empty() ? "" : diag.errors[0]));
	Model m;
	std::string error;
	REQUIRE_MESSAGE(loadModel(artifact, m, &error), error);

	REQUIRE(m.nodes.size() == 4);
	CHECK(m.nodes[1].name == "BodyMesh");
	CHECK(m.nodes[1].parent == 0);
	CHECK(m.nodes[1].mesh == 0);
	CHECK(m.nodes[1].skin == 0);
	CHECK(m.nodes[3].parent == 2);
	CHECK(m.nodes[3].rotation[1] == doctest::Approx(0.7071068f));

	REQUIRE(m.slots.size() == 2);
	CHECK(m.slots[0].name == "Skin");
	CHECK(m.slots[0].baseColor[1] == doctest::Approx(0.5f));
	CHECK(m.slots[0].roughness == doctest::Approx(0.8f));
	CHECK(m.slots[0].baseColorTexture.path == "textures/albedo.png");
	CHECK(m.slots[1].alphaMode == AlphaMode::Mask);
	CHECK(m.slots[1].doubleSided);

	REQUIRE(m.meshes.size() == 1);
	const ModelMesh& mesh = m.meshes[0];
	CHECK(mesh.name == "Body");
	CHECK(mesh.vertexCount == 7);
	CHECK(mesh.indices.size() == 9);
	REQUIRE(mesh.submeshes.size() == 2);
	CHECK(mesh.submeshes[0].indexCount == 6);
	CHECK(mesh.submeshes[0].slot == 0);
	CHECK(mesh.submeshes[1].indexOffset == 6);
	CHECK(mesh.submeshes[1].slot == 1);
	CHECK(mesh.indices[6] == 4);   // 2 つ目の primitive の頂点は 4 から
	CHECK(mesh.uv0.size() == 14);
	CHECK(mesh.tangents.size() == 28);
	CHECK(mesh.tangents[0] == doctest::Approx(1.0f));   // U が +X の向き
	CHECK(mesh.boundsMax[0] == doctest::Approx(3.0f));

	// スキン: 重みは合計 1、大きい順
	REQUIRE(m.skins.size() == 1);
	CHECK(m.skins[0].joints == std::vector<int32_t>{ 2, 3 });
	CHECK(m.skins[0].inverseBind[16 + 12] == doctest::Approx(-1.0f));
	for (size_t v = 0; v < mesh.vertexCount; ++v)
		CHECK(sumWeights(mesh, v) == doctest::Approx(1.0f));
	CHECK(mesh.weights[3 * 4] == doctest::Approx(1.0f));

	// モーフ: 四角形の頂点 0 / 1 だけが Z に 0.5、三角形の頂点は 0
	REQUIRE(mesh.morphs.size() == 1);
	CHECK(mesh.morphs[0].name == "Smile");
	CHECK(mesh.morphs[0].defaultWeight == doctest::Approx(0.25f));
	CHECK(mesh.morphs[0].positions[2] == doctest::Approx(0.5f));
	CHECK(mesh.morphs[0].positions[2 * 3 + 2] == doctest::Approx(0.0f));
	CHECK(mesh.morphs[0].positions.size() == 21);

	// 全体の境界は Root の平行移動 (z = -5) を含む
	CHECK(m.boundsMin[2] == doctest::Approx(-5.0f));

	// 設定の scale で大きさを変える (作り直す)
	AssetMeta meta = r->meta;
	meta.settings = { { "scale", 2.0 } };
	REQUIRE(writeMeta(r->metaFile, meta));
	f.db.scan(root, diag);
	REQUIRE(f.db.import(*f.db.findByPath("hero.gltf"), cache, artifact, diag));
	Model scaled;
	REQUIRE(loadModel(artifact, scaled));
	CHECK(scaled.meshes[0].boundsMax[0] == doctest::Approx(6.0f));
	CHECK(scaled.nodes[0].translation[2] == doctest::Approx(-10.0f));
	CHECK(scaled.skins[0].inverseBind[16 + 12] == doctest::Approx(-2.0f));

	fs::remove_all(root);
	fs::remove_all(cache);
}

TEST_CASE("モデル: FBX (ASCII) を取り込む。cm を m に変換し、マテリアルごとのサブメッシュを作る")
{
	const fs::path root = freshDir("sasset_model_fbx"), cache = freshDir("sasset_model_fbx_cache");
	writeBytes(root / "quad.fbx", k_Fbx);
	Fixture f;
	Diagnostics diag;
	f.db.scan(root, diag);
	fs::path artifact;
	REQUIRE_MESSAGE(f.db.import(*f.db.findByPath("quad.fbx"), cache, artifact, diag), (diag.errors.empty() ? "" : diag.errors[0]));
	Model m;
	REQUIRE(loadModel(artifact, m));
	REQUIRE(m.meshes.size() == 1);
	const ModelMesh& mesh = m.meshes[0];
	CHECK(mesh.vertexCount == 4);
	CHECK(mesh.indices.size() == 6);
	CHECK(mesh.boundsMax[0] == doctest::Approx(1.0f).epsilon(1e-4));   // 100 cm → 1 m
	CHECK(mesh.boundsMax[1] == doctest::Approx(1.0f).epsilon(1e-4));   // Y が上のまま
	CHECK(mesh.normals[2] == doctest::Approx(1.0f).epsilon(1e-4));     // 法線は作られる (+Z)
	REQUIRE(mesh.submeshes.size() == 1);
	REQUIRE(m.slots.size() >= 1);
	const MaterialSlot& slot = m.slots[mesh.submeshes[0].slot];
	CHECK(slot.name == "Red");
	CHECK(slot.baseColor[0] == doctest::Approx(1.0f));
	CHECK(slot.baseColor[1] == doctest::Approx(0.0f));
	// ノードの平行移動も m に (200 cm → 2 m)
	bool found = false;
	for (const auto& n : m.nodes)
		if (n.mesh == 0)
		{
			found = true;
			CHECK(n.name == "Quad");
		}
	CHECK(found);
	CHECK(m.boundsMax[0] == doctest::Approx(3.0f).epsilon(1e-4));

	fs::remove_all(root);
	fs::remove_all(cache);
}

TEST_CASE("モデル: OBJ も ufbx で読む")
{
	const fs::path root = freshDir("sasset_model_obj"), cache = freshDir("sasset_model_obj_cache");
	writeBytes(root / "tri.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n");
	Fixture f;
	Diagnostics diag;
	f.db.scan(root, diag);
	fs::path artifact;
	REQUIRE(f.db.import(*f.db.findByPath("tri.obj"), cache, artifact, diag));
	Model m;
	REQUIRE(loadModel(artifact, m));
	REQUIRE(m.meshes.size() == 1);
	CHECK(m.meshes[0].vertexCount == 3);
	CHECK(m.meshes[0].uv0[1] == doctest::Approx(1.0f));   // V は上下を反転 (左上が原点)
	fs::remove_all(root);
	fs::remove_all(cache);
}

TEST_CASE("モデル: 壊れたファイルはエラー")
{
	const fs::path root = freshDir("sasset_model_bad"), cache = freshDir("sasset_model_bad_cache");
	writeBytes(root / "bad.gltf", "{ not json");
	writeBytes(root / "bad.fbx", "garbage");
	Fixture f;
	Diagnostics diag;
	f.db.scan(root, diag);
	fs::path artifact;
	Diagnostics d1, d2;
	CHECK_FALSE(f.db.import(*f.db.findByPath("bad.gltf"), cache, artifact, d1));
	CHECK_FALSE(d1.errors.empty());
	CHECK_FALSE(f.db.import(*f.db.findByPath("bad.fbx"), cache, artifact, d2));
	CHECK_FALSE(d2.errors.empty());
	fs::remove_all(root);
	fs::remove_all(cache);
}
