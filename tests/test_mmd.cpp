// ============================================================
//  MMD のモデル (PMX / PMD) の取り込みの単体テスト。ファイルはテストの中でバイト列として書く (配布モデルは使わない)
// ============================================================
#include <doctest/doctest.h>
#include "sunset/asset/AssetDatabase.h"
#include "sunset/asset/model/ModelImporter.h"

#include <cstring>
#include <fstream>

using namespace sunset::asset;
namespace fs = std::filesystem;

namespace
{
// リトルエンディアンでバイト列を積む
struct Bytes
{
	std::string b;
	template<typename T> Bytes& put(T v) { char tmp[sizeof(T)]; std::memcpy(tmp, &v, sizeof(T)); b.append(tmp, sizeof(T)); return *this; }
	Bytes& f(float x) { return put(x); }
	Bytes& f3(float x, float y, float z) { return f(x).f(y).f(z); }
	Bytes& f4(float x, float y, float z, float w) { return f(x).f(y).f(z).f(w); }
	Bytes& raw(const std::string& s) { b += s; return *this; }
	// PMX の文字 (UTF-16LE。ASCII と、ここで使う少しの日本語だけ)
	Bytes& text(const std::u16string& s)
	{
		put<int32_t>(static_cast<int32_t>(s.size() * 2));
		for (char16_t c : s) put<uint16_t>(static_cast<uint16_t>(c));
		return *this;
	}
	Bytes& fixed(const std::string& s, size_t n) { std::string t = s; t.resize(n, '\0'); b += t; return *this; }
};

void writeBytes(const fs::path& p, const std::string& s)
{
	std::ofstream(p, std::ios::binary).write(s.data(), static_cast<std::streamsize>(s.size()));
}

fs::path freshDir(const char* name)
{
	const fs::path p = fs::temp_directory_path() / name;
	std::error_code ec;
	fs::remove_all(p, ec);
	fs::create_directories(p);
	return p;
}

// 4 頂点 (BDEF1 / BDEF2 / BDEF4 / SDEF)、2 材質、2 骨 (IK と付与親)、モーフ 3 つ、表示枠、剛体、ジョイント
// toonB: 材質 B の個別のトゥーンのテクスチャの番号 (-1 はなし。配布モデルによくある)
std::string makePmx(int8_t toonB = 0)
{
	Bytes w;
	w.raw("PMX ").f(2.0f);
	w.put<uint8_t>(8);
	const uint8_t g[8] = { 0, 1, 1, 1, 1, 1, 1, 1 };   // UTF-16、追加 UV 1、番号はすべて 1 バイト
	for (uint8_t x : g) w.put(x);
	w.text(u"テスト").text(u"test").text(u"comment").text(u"");
	// 頂点
	w.put<int32_t>(4);
	auto vtx = [&](float x, float y, float z, float u, float v) {
		w.f3(x, y, z).f3(0, 0, 1).f(u).f(v).f4(0.5f, 0.25f, 0, 0);
	};
	vtx(1, 2, 3, 0, 0);     w.put<uint8_t>(0).put<int8_t>(0).f(1.0f);
	vtx(-1, 2, 3, 1, 0);    w.put<uint8_t>(1).put<int8_t>(0).put<int8_t>(1).f(0.7f).f(1.0f);
	vtx(-1, 0, 3, 1, 1);    w.put<uint8_t>(2).put<int8_t>(0).put<int8_t>(1).put<int8_t>(0).put<int8_t>(1).f(0.1f).f(0.2f).f(0.3f).f(0.4f).f(1.0f);
	vtx(1, 0, 3, 0, 1);     w.put<uint8_t>(3).put<int8_t>(0).put<int8_t>(1).f(0.6f).f3(0, 0, 0).f3(0, 0, 0).f3(0, 0, 0).f(1.0f);
	// 面: (0, 1, 2) と (0, 2, 3)
	w.put<int32_t>(6);
	for (int i : { 0, 1, 2, 0, 2, 3 }) w.put<uint8_t>(static_cast<uint8_t>(i));
	// テクスチャ
	w.put<int32_t>(2).text(u"tex\\a.png").text(u"sph\\s.sph");
	// 材質 A: 両面 + 輪郭線、テクスチャ 0、スフィア 1 (乗算)、共有トゥーン 3
	w.put<int32_t>(2);
	w.text(u"体").text(u"body").f4(1, 0.5f, 0.25f, 1).f3(0.1f, 0.1f, 0.1f).f(50).f3(0.5f, 0.5f, 0.5f).put<uint8_t>(0x01 | 0x04 | 0x10);
	w.f4(0, 0, 0, 1).f(1.0f).put<int8_t>(0).put<int8_t>(1).put<uint8_t>(1).put<uint8_t>(1).put<uint8_t>(3).text(u"").put<int32_t>(3);
	// 材質 B: 半透明、テクスチャなし、個別のトゥーン (テクスチャ toonB)
	w.text(u"髪").text(u"hair").f4(0.2f, 0.2f, 0.2f, 0.5f).f3(0, 0, 0).f(5).f3(0, 0, 0).put<uint8_t>(0);
	w.f4(0, 0, 0, 1).f(1.0f).put<int8_t>(-1).put<int8_t>(-1).put<uint8_t>(0).put<uint8_t>(0).put<int8_t>(toonB).text(u"memo").put<int32_t>(3);
	// 骨 0: センター (0, 10, 0)、骨 1: 頭 (0, 15, 5)。骨 1 は IK (リンクは骨 0、範囲あり) と回転の付与親 (骨 0, 0.5)
	w.put<int32_t>(2);
	w.text(u"センター").text(u"center").f3(0, 10, 0).put<int8_t>(-1).put<int32_t>(0).put<uint16_t>(0x0001 | 0x0002 | 0x0008 | 0x0010).put<int8_t>(1);
	w.text(u"頭").text(u"head").f3(0, 15, 5).put<int8_t>(0).put<int32_t>(0).put<uint16_t>(0x0002 | 0x0020 | 0x0100).f3(0, 1, 0);
	w.put<int8_t>(0).f(0.5f);                                      // 付与親
	w.put<int8_t>(0).put<int32_t>(40).f(2.0f).put<int32_t>(1);      // IK: 対象 0、40 回、制限角 2
	w.put<int8_t>(0).put<uint8_t>(1).f3(-1, -2, -3).f3(1, 2, 3);   // リンク 0 (範囲あり)
	// モーフ: 頂点 (頂点 1 を (0, 1, 2) 動かす)、グループ (0 番を 0.5)、骨 (骨 1 を動かして回す)
	w.put<int32_t>(3);
	w.text(u"あ").text(u"a").put<uint8_t>(3).put<uint8_t>(1).put<int32_t>(1).put<uint8_t>(1).f3(0, 1, 2);
	w.text(u"まとめ").text(u"group").put<uint8_t>(4).put<uint8_t>(0).put<int32_t>(1).put<int8_t>(0).f(0.5f);
	w.text(u"骨").text(u"bone").put<uint8_t>(4).put<uint8_t>(2).put<int32_t>(1).put<int8_t>(1).f3(0, 0, 1).f4(0.1f, 0.2f, 0.3f, 0.9f);
	// 表示枠: 骨 0
	w.put<int32_t>(1).text(u"Root").text(u"Root").put<uint8_t>(1).put<int32_t>(1).put<uint8_t>(0).put<int8_t>(0);
	// 剛体: 箱 (1, 2, 3)、位置 (0, 1, 2)、回転 (0.1, 0.2, 0.3)
	w.put<int32_t>(1).text(u"剛体").text(u"body").put<int8_t>(0).put<uint8_t>(1).put<uint16_t>(0xFFFF).put<uint8_t>(1);
	w.f3(1, 2, 3).f3(0, 1, 2).f3(0.1f, 0.2f, 0.3f).f(1).f(0.5f).f(0.5f).f(0).f(0.5f).put<uint8_t>(1);
	// ジョイント
	w.put<int32_t>(1).text(u"J").text(u"J").put<uint8_t>(0).put<int8_t>(0).put<int8_t>(0);
	w.f3(0, 0, 1).f3(0, 0, 0).f3(-1, -1, -1).f3(1, 1, 2).f3(-0.5f, -0.5f, -0.5f).f3(0.5f, 0.5f, 0.5f).f3(0, 0, 0).f3(0, 0, 0);
	return w.b;
}

std::string makePmd()
{
	Bytes w;
	w.raw("Pmd").f(1.0f);
	// 名前は Shift_JIS ("センター")
	w.fixed("\x83\x5A\x83\x93\x83\x5E\x81\x5B", 20).fixed("comment", 256);
	w.put<uint32_t>(3);
	auto vtx = [&](float x, float y, float z, uint16_t b0, uint16_t b1, uint8_t weight) {
		w.f3(x, y, z).f3(0, 1, 0).f(0).f(0).put(b0).put(b1).put<uint8_t>(weight).put<uint8_t>(0);
	};
	vtx(0, 0, 0, 0, 0, 100);
	vtx(10, 0, 0, 0, 1, 25);
	vtx(0, 10, 5, 1, 1, 100);
	w.put<uint32_t>(3).put<uint16_t>(0).put<uint16_t>(1).put<uint16_t>(2);
	// 材質: "a.bmp*b.sph"、トゥーン 2 番
	w.put<uint32_t>(1).f3(0.8f, 0.6f, 0.4f).f(1.0f).f(10).f3(0, 0, 0).f3(0.2f, 0.2f, 0.2f).put<uint8_t>(2).put<uint8_t>(1).put<uint32_t>(3);
	w.fixed("a.bmp*b.sph", 20);
	// 骨 2 つ
	w.put<uint16_t>(2);
	w.fixed("\x83\x5A\x83\x93\x83\x5E\x81\x5B", 20).put<uint16_t>(0xFFFF).put<uint16_t>(1).put<uint8_t>(0).put<uint16_t>(0).f3(0, 0, 0);
	w.fixed("bone1", 20).put<uint16_t>(0).put<uint16_t>(0).put<uint8_t>(0).put<uint16_t>(0).f3(0, 10, 5);
	// IK 1 つ
	w.put<uint16_t>(1).put<uint16_t>(1).put<uint16_t>(0).put<uint8_t>(1).put<uint16_t>(15).f(0.5f).put<uint16_t>(0);
	// 表情: base (頂点 2 だけ) と、"a" (base の 0 番 = 頂点 2 を (0, 0, 1) 動かす)
	w.put<uint16_t>(2);
	w.fixed("base", 20).put<uint32_t>(1).put<uint8_t>(0).put<uint32_t>(2).f3(0, 10, 5);
	w.fixed("a", 20).put<uint32_t>(1).put<uint8_t>(1).put<uint32_t>(0).f3(0, 0, 1);
	// 表示、英語名なし、トゥーン (2 番は "toonX.bmp")、剛体 0、ジョイント 0
	w.put<uint8_t>(0).put<uint8_t>(0).put<uint32_t>(0).put<uint8_t>(0);
	for (int i = 0; i < 10; ++i) w.fixed(i == 2 ? "toonX.bmp" : "", 100);
	w.put<uint32_t>(0).put<uint32_t>(0);
	return w.b;
}

struct Fixture
{
	ImporterRegistry reg;
	AssetDatabase    db { reg };
	Fixture() { reg.add(makeModelImporter()); }
};

Model importFile(const fs::path& root, const fs::path& cache, const char* name, Diagnostics& diag, const Json& settings = Json())
{
	Fixture f;
	f.db.scan(root, diag);
	const AssetRecord* r = f.db.findByPath(name);
	REQUIRE(r);
	if (!settings.is_null())
	{
		AssetMeta m = r->meta;
		m.settings = settings;
		REQUIRE(writeMeta(r->metaFile, m));
		f.db.scan(root, diag);
		r = f.db.findByPath(name);
	}
	fs::path artifact;
	REQUIRE_MESSAGE(f.db.import(*r, cache, artifact, diag), (diag.errors.empty() ? std::string() : diag.errors[0]));
	Model m;
	REQUIRE(loadModel(artifact, m));
	return m;
}
} // namespace

TEST_CASE("MMD: PMX を読む (座標、重み、材質、骨、モーフ、物理)")
{
	const fs::path root = freshDir("sasset_pmx"), cache = freshDir("sasset_pmx_cache");
	writeBytes(root / "test.pmx", makePmx());
	Diagnostics diag;
	const Model m = importFile(root, cache, "test.pmx", diag);
	REQUIRE(m.meshes.size() == 1);
	const ModelMesh& mesh = m.meshes[0];
	CHECK(mesh.name == "テスト");
	REQUIRE(mesh.vertexCount == 4);
	// 座標: × 0.08、Z を反転
	CHECK(mesh.positions[0] == doctest::Approx(0.08f));
	CHECK(mesh.positions[1] == doctest::Approx(0.16f));
	CHECK(mesh.positions[2] == doctest::Approx(-0.24f));
	CHECK(mesh.normals[2] == doctest::Approx(-1.0f));
	CHECK(mesh.uv1[0] == doctest::Approx(0.5f));   // 追加 UV 1 の xy
	// 三角形の向きを入れ替える: (0, 1, 2) → (0, 2, 1)
	REQUIRE(mesh.indices.size() == 6);
	CHECK(mesh.indices[0] == 0);
	CHECK(mesh.indices[1] == 2);
	CHECK(mesh.indices[2] == 1);
	// 重み: BDEF2 (0.7 / 0.3)、SDEF → BDEF2
	CHECK(mesh.weights[4] == doctest::Approx(0.7f));
	CHECK(mesh.weights[5] == doctest::Approx(0.3f));
	CHECK(mesh.weights[12] == doctest::Approx(0.6f));
	CHECK(diag.warnings.size() >= 1);   // SDEF を近似した
	// 材質
	REQUIRE(m.slots.size() == 2);
	REQUIRE(mesh.submeshes.size() == 2);
	CHECK(mesh.submeshes[1].indexOffset == 3);
	CHECK(m.slots[0].name == "体");
	CHECK(m.slots[0].doubleSided);
	CHECK(m.slots[0].baseColorTexture.path == "tex/a.png");
	CHECK(m.slots[0].extras["mmd"]["toon"]["file"] == "toon04.bmp");
	CHECK(m.slots[0].extras["mmd"]["sphere"]["file"] == "sph/s.sph");
	CHECK(m.slots[0].extras["mmd"]["sphere"]["mode"] == "multiply");
	CHECK(m.slots[0].extras["mmd"]["edge"]["enabled"] == true);
	CHECK(m.slots[0].extras["mmd"]["castShadow"] == true);
	CHECK(m.slots[1].alphaMode == AlphaMode::Blend);
	CHECK(m.slots[1].extras["mmd"]["toon"]["file"] == "tex/a.png");
	// 骨: ノード 0 = メッシュ、骨はノード 1 から。位置は親からの相対
	REQUIRE(m.nodes.size() == 3);
	CHECK(m.nodes[0].mesh == 0);
	CHECK(m.nodes[0].skin == 0);
	CHECK(m.nodes[2].name == "頭");
	CHECK(m.nodes[2].parent == 1);
	CHECK(m.nodes[2].translation[1] == doctest::Approx(0.4f));
	CHECK(m.nodes[2].translation[2] == doctest::Approx(-0.4f));
	const Json& head = m.nodes[2].extras["mmd"];
	CHECK(head["inherit"]["parent"] == 1);
	CHECK(head["inherit"]["weight"] == doctest::Approx(0.5));
	CHECK(head["ik"]["target"] == 1);
	CHECK(head["ik"]["loop"] == 40);
	CHECK(head["ik"]["links"][0]["min"][0] == doctest::Approx(-1.0));   // X の範囲は反転: (-1, 1) → (-1, 1)
	CHECK(head["ik"]["links"][0]["min"][1] == doctest::Approx(-2.0));
	CHECK(head["ik"]["links"][0]["max"][2] == doctest::Approx(3.0));    // Z の回りはそのまま
	CHECK(m.nodes[1].extras["mmd"]["tailBone"] == 2);
	REQUIRE(m.skins.size() == 1);
	CHECK(m.skins[0].joints == std::vector<int32_t>{ 1, 2 });
	CHECK(m.skins[0].inverseBind[13] == doctest::Approx(-0.8f));   // 骨 0 の位置 (0, 0.8, 0) を戻す
	// モーフ: 頂点モーフはメッシュに (差分も同じ変換)、ほかは extras に
	REQUIRE(mesh.morphs.size() == 1);
	CHECK(mesh.morphs[0].name == "あ");
	CHECK(mesh.morphs[0].positions[4] == doctest::Approx(0.08f));
	CHECK(mesh.morphs[0].positions[5] == doctest::Approx(-0.16f));
	const Json& mmd = m.extras["mmd"];
	CHECK(mmd["format"] == "pmx");
	REQUIRE(mmd["morphs"].size() == 2);
	CHECK(mmd["morphs"][0]["type"] == "group");
	CHECK(mmd["morphs"][1]["offsets"][0]["bone"] == 2);
	CHECK(mmd["morphs"][1]["offsets"][0]["rotation"][0] == doctest::Approx(-0.1));
	CHECK(mmd["morphs"][1]["offsets"][0]["translation"][2] == doctest::Approx(-0.08));
	// 表示枠、剛体、ジョイント
	CHECK(mmd["displayFrames"][0]["items"][0]["bone"] == 1);
	REQUIRE(mmd["rigidBodies"].size() == 1);
	CHECK(mmd["rigidBodies"][0]["shape"] == "box");
	CHECK(mmd["rigidBodies"][0]["size"][2] == doctest::Approx(0.24));
	CHECK(mmd["rigidBodies"][0]["position"][2] == doctest::Approx(-0.16));
	CHECK(mmd["rigidBodies"][0]["rotation"][0] == doctest::Approx(-0.1));
	CHECK(mmd["rigidBodies"][0]["rotation"][2] == doctest::Approx(0.3));
	CHECK(mmd["joints"][0]["translationMin"][2] == doctest::Approx(-0.16));   // Z の範囲 (-1, 2) → (-2, 1) × 0.08
	CHECK(mmd["joints"][0]["translationMax"][2] == doctest::Approx(0.08));
	// 境界と接線
	CHECK(m.boundsMax[1] == doctest::Approx(0.16f));
	CHECK(mesh.tangents.size() == 16);

	// 大きさの設定は MMD の単位に掛ける (剛体にも)
	Diagnostics d2;
	const Model big = importFile(root, freshDir("sasset_pmx_cache2"), "test.pmx", d2, { { "scale", 2.0 } });
	CHECK(big.meshes[0].positions[0] == doctest::Approx(0.16f));
	CHECK(big.extras["mmd"]["rigidBodies"][0]["size"][0] == doctest::Approx(0.16));

	// 参照するファイル (テクスチャ / スフィア。共有のトゥーンは含めない)
	Fixture f;
	Diagnostics rd;
	const auto refs = f.reg.forFile(root / "test.pmx")->referencedFiles(root / "test.pmx", rd);
	CHECK(refs == std::vector<std::string>{ "tex/a.png", "sph/s.sph" });

	// 個別のトゥーンでテクスチャなし (-1): トゥーンなしとして読み、参照するファイルにも含めない
	writeBytes(root / "notoon.pmx", makePmx(-1));
	Diagnostics nd;
	const Model nt = importFile(root, freshDir("sasset_pmx_cache3"), "notoon.pmx", nd);
	CHECK(nt.slots[1].extras["mmd"]["toon"].is_null());
	const auto nrefs = f.reg.forFile(root / "notoon.pmx")->referencedFiles(root / "notoon.pmx", nd);
	CHECK(nrefs == std::vector<std::string>{ "tex/a.png", "sph/s.sph" });

	// 壊れたファイル
	writeBytes(root / "broken.pmx", makePmx().substr(0, 200));
	Fixture fb;
	Diagnostics bd;
	fb.db.scan(root, bd);
	fs::path artifact;
	CHECK_FALSE(fb.db.import(*fb.db.findByPath("broken.pmx"), cache, artifact, bd));
	CHECK_FALSE(bd.ok());
	writeBytes(root / "bad.pmx", "PMY nothing");
	Diagnostics bd2;
	fb.db.scan(root, bd2);
	CHECK_FALSE(fb.db.import(*fb.db.findByPath("bad.pmx"), cache, artifact, bd2));

	std::error_code ec;
	fs::remove_all(root, ec);
	fs::remove_all(cache, ec);
}

TEST_CASE("MMD: PMD を読む (Shift_JIS、重み、テクスチャとスフィア、表情、トゥーン)")
{
	const fs::path root = freshDir("sasset_pmd"), cache = freshDir("sasset_pmd_cache");
	writeBytes(root / "old.pmd", makePmd());
	Diagnostics diag;
	const Model m = importFile(root, cache, "old.pmd", diag);
	REQUIRE(m.meshes.size() == 1);
	const ModelMesh& mesh = m.meshes[0];
#ifdef _WIN32
	CHECK(mesh.name == "センター");
	CHECK(m.nodes[1].name == "センター");
#endif
	REQUIRE(mesh.vertexCount == 3);
	CHECK(mesh.positions[3] == doctest::Approx(0.8f));
	CHECK(mesh.positions[8] == doctest::Approx(-0.4f));
	// 頂点 1: 骨 0 が 25%、骨 1 が 75% (重みの大きい順に並ぶ)
	for (int k = 0; k < 2; ++k)
		CHECK(mesh.weights[4 + k] == doctest::Approx(mesh.joints[4 + k] == 0 ? 0.25f : 0.75f));
	CHECK(mesh.joints[4] != mesh.joints[5]);
	CHECK(mesh.indices == std::vector<uint32_t>{ 0, 2, 1 });
	REQUIRE(m.slots.size() == 1);
	CHECK(m.slots[0].baseColorTexture.path == "a.bmp");
	CHECK(m.slots[0].extras["mmd"]["sphere"]["file"] == "b.sph");
	CHECK(m.slots[0].extras["mmd"]["toon"]["file"] == "toonX.bmp");
	REQUIRE(mesh.morphs.size() == 1);
	CHECK(mesh.morphs[0].name == "a");
	CHECK(mesh.morphs[0].positions[8] == doctest::Approx(-0.08f));   // 頂点 2 の Z
	CHECK(m.extras["mmd"]["format"] == "pmd");
	CHECK(m.extras["mmd"]["ik"][0]["target"] == 1);
	CHECK(m.nodes[2].parent == 1);

	std::error_code ec;
	fs::remove_all(root, ec);
	fs::remove_all(cache, ec);
}
