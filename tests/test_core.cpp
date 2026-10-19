// ============================================================
//  core の単体テスト: .meta、取り込みの登録、キャッシュの入れ物、データベース
// ============================================================
#include <doctest/doctest.h>
#include "sunset/asset/AssetDatabase.h"

#include <chrono>
#include <fstream>
#include <thread>

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

void writeText(const fs::path& p, const std::string& text)
{
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << text;
}

bool hasText(const std::vector<std::string>& v, const char* part)
{
	for (const auto& s : v)
		if (s.find(part) != std::string::npos)
			return true;
	return false;
}

// テスト用の取り込み: ファイルの中身を "TEXT" に、設定の "upper" が true なら大文字にして入れる
class TextImporter final : public Importer
{
public:
	explicit TextImporter(uint32_t version = 1) : m_version(version) {}
	std::string_view         name() const override { return "text"; }
	std::vector<std::string> extensions() const override { return { ".txt", ".note.json" }; }
	uint32_t                 version() const override { return m_version; }
	Json                     defaultSettings() const override { return { { "upper", false }, { "nested", { { "a", 1 }, { "b", 2 } } } }; }
	bool import(const ImportContext& ctx, ArtifactWriter& out, Diagnostics& diag) const override
	{
		std::string text;
		if (!readFile(ctx.source, text))
			return diag.error("読めません"), false;
		if (text == "fail")
			return diag.error("わざと失敗"), false;
		if (ctx.settings.value("upper", false))
			for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		out.add(chunkTag("TEXT"), text);
		++imports;
		return true;
	}
	mutable int imports = 0;

private:
	uint32_t m_version;
};
} // namespace

TEST_CASE(".meta: 読み書きと診断")
{
	AssetMeta m;
	m.id       = Guid::generate();
	m.importer = "model";
	m.settings = { { "scale", 0.01 } };
	AssetMeta back;
	Diagnostics diag;
	REQUIRE(metaFromJson(metaToJson(m), back, diag));
	CHECK(back.id == m.id);
	CHECK(back.importer == "model");
	CHECK(back.settings["scale"] == 0.01);
	CHECK_FALSE(back.external());
	CHECK_FALSE(metaToJson(m).contains("source"));

	m.source = "D:/work/hero.fbx";
	REQUIRE(metaFromJson(metaToJson(m), back, diag));
	CHECK(back.external());

	Diagnostics bad;
	CHECK_FALSE(metaFromJson({ { "id", "xyz" }, { "importer", "" }, { "future", 1 } }, back, bad));
	CHECK(hasText(bad.errors, "id"));
	CHECK(hasText(bad.errors, "importer"));
	CHECK(hasText(bad.warnings, "future"));
	Diagnostics noId;
	CHECK_FALSE(metaFromJson({ { "importer", "model" } }, back, noId));

	CHECK(metaPathFor("a/b.fbx") == fs::path("a/b.fbx.meta"));
	CHECK(assetPathForMeta("a/b.fbx.meta") == fs::path("a/b.fbx"));
}

TEST_CASE("取り込みの登録: 名前、拡張子、設定の重ね合わせ")
{
	ImporterRegistry reg;
	CHECK(reg.add(std::make_unique<TextImporter>()));
	CHECK_FALSE(reg.add(std::make_unique<TextImporter>()));   // 同じ名前
	CHECK(reg.byName("text") != nullptr);
	CHECK(reg.forFile("x/Readme.TXT") == reg.byName("text"));
	CHECK(reg.forFile("x/a.note.json") == reg.byName("text"));   // 2 段の拡張子
	CHECK(reg.forFile("x/a.json") == nullptr);
	CHECK(reg.forFile(".txt") == nullptr);                         // 拡張子だけの名前は除く

	const Json merged = ImporterRegistry::mergeSettings({ { "upper", false }, { "nested", { { "a", 1 }, { "b", 2 } } } },
	                                                    { { "upper", true }, { "nested", { { "b", 3 } } } });
	CHECK(merged["upper"] == true);
	CHECK(merged["nested"]["a"] == 1);
	CHECK(merged["nested"]["b"] == 3);
}

TEST_CASE("キャッシュの入れ物: 書いて読む、壊れたものを断る")
{
	ArtifactHeader h;
	h.importer = "text";
	h.importerVersion = 3;
	h.sourceSize = 5;
	h.sourceTime = 123;
	h.sourceHash = 0xABCDEF;
	h.settingsHash = 42;
	ArtifactWriter w;
	w.add(chunkTag("TEXT"), std::string("hello"));
	const float values[] = { 1, 2, 3 };
	w.addArray<float>(chunkTag("FLTS"), values);
	const std::string bytes = w.serialize(h);

	ArtifactReader r;
	std::string error;
	REQUIRE(r.parse(bytes, &error));
	CHECK(r.header().importer == "text");
	CHECK(r.header().importerVersion == 3);
	CHECK(r.header().sameInputs(h));
	CHECK(r.chunkCount() == 2);
	const auto text = r.find(chunkTag("TEXT"));
	CHECK(std::string(text.data(), text.size()) == "hello");
	CHECK(r.array<float>(chunkTag("FLTS")) == std::vector<float>{ 1, 2, 3 });
	CHECK(r.find(chunkTag("NONE")).empty());

	ArtifactReader broken;
	CHECK_FALSE(broken.parse(bytes.substr(0, bytes.size() - 2), &error));   // 途中で切れている
	CHECK_FALSE(broken.parse("XXXX" + bytes.substr(4), &error));            // 目印が違う
	ArtifactHeader other = h;
	other.settingsHash = 43;
	CHECK_FALSE(h.sameInputs(other));
}

TEST_CASE("データベース: .meta を作り、ID で引き、取り込みをキャッシュする")
{
	const fs::path root  = freshDir("sasset_db_root");
	const fs::path cache = freshDir("sasset_db_cache");
	writeText(root / "notes" / "a.txt", "alpha");
	writeText(root / "b.txt", "beta");
	writeText(root / "image.png", "not registered");   // 取り込みのない拡張子には .meta を作らない

	ImporterRegistry reg;
	reg.add(std::make_unique<TextImporter>());
	auto* importer = static_cast<const TextImporter*>(reg.byName("text"));
	AssetDatabase db(reg);
	Diagnostics diag;
	const auto r1 = db.scan(root, diag);
	CHECK(diag.ok());
	CHECK(r1.created == 2);
	CHECK(fs::exists(root / "notes" / "a.txt.meta"));
	CHECK_FALSE(fs::exists(root / "image.png.meta"));
	REQUIRE(db.records().size() == 2);
	const AssetRecord* a = db.findByPath("notes/a.txt");
	REQUIRE(a);
	CHECK(db.find(a->id) == a);
	const Guid aId = a->id;

	// もう一度走査しても ID は変わらず、.meta も作らない
	const auto r2 = db.scan(root, diag);
	CHECK(r2.created == 0);
	CHECK(db.findByPath("notes/a.txt")->id == aId);

	// 取り込み: 初回は取り込み、2 回目はキャッシュを使う
	fs::path artifact;
	REQUIRE(db.import(*db.find(aId), cache, artifact, diag));
	CHECK(importer->imports == 1);
	CHECK(db.upToDate(*db.find(aId), cache));
	REQUIRE(db.import(*db.find(aId), cache, artifact, diag));
	CHECK(importer->imports == 1);
	ArtifactReader rd;
	REQUIRE(rd.open(artifact));
	CHECK(std::string(rd.find(chunkTag("TEXT")).data(), 5) == "alpha");
	CHECK(rd.header().sourceHash == hash64(std::string("alpha")));

	// 設定を変えると作り直す
	AssetMeta m = db.find(aId)->meta;
	m.settings = { { "upper", true } };
	REQUIRE(writeMeta(db.find(aId)->metaFile, m));
	db.scan(root, diag);
	CHECK_FALSE(db.upToDate(*db.find(aId), cache));
	REQUIRE(db.import(*db.find(aId), cache, artifact, diag));
	CHECK(importer->imports == 2);
	REQUIRE(rd.open(artifact));
	CHECK(std::string(rd.find(chunkTag("TEXT")).data(), 5) == "ALPHA");

	// 元のファイルを変えると作り直す (大きさが変わる)
	writeText(root / "notes" / "a.txt", "alpha2");
	CHECK_FALSE(db.upToDate(*db.find(aId), cache));

	// 取り込みの版を上げると作り直す
	ImporterRegistry reg2;
	reg2.add(std::make_unique<TextImporter>(2));
	AssetDatabase db2(reg2);
	db2.scan(root, diag);
	REQUIRE(db.import(*db.find(aId), cache, artifact, diag));
	CHECK_FALSE(db2.upToDate(*db2.find(aId), cache));

	// 取り込みの失敗は diag に入る
	writeText(root / "b.txt", "fail");
	Diagnostics fail;
	CHECK_FALSE(db.import(*db.findByPath("b.txt"), cache, artifact, fail));
	CHECK(hasText(fail.errors, "わざと失敗"));

	fs::remove_all(root);
	fs::remove_all(cache);
}

TEST_CASE("データベース: 重なった ID、残った .meta、外部の参照")
{
	const fs::path root = freshDir("sasset_db_dup");
	const fs::path work = freshDir("sasset_db_external");
	ImporterRegistry reg;
	reg.add(std::make_unique<TextImporter>());
	AssetDatabase db(reg);
	Diagnostics diag;

	// ファイルを .meta ごと複製すると ID が重なる → 後のほうに新しい ID
	writeText(root / "a.txt", "a");
	db.scan(root, diag);
	fs::copy_file(root / "a.txt", root / "copy.txt");
	fs::copy_file(root / "a.txt.meta", root / "copy.txt.meta");
	Diagnostics d2;
	const auto r = db.scan(root, d2);
	CHECK(r.reIded == 1);
	CHECK(hasText(d2.warnings, "重なって"));
	CHECK(db.findByPath("a.txt")->id != db.findByPath("copy.txt")->id);

	// ファイルを消して .meta だけが残る → missing
	fs::remove(root / "copy.txt");
	Diagnostics d3;
	db.scan(root, d3);
	CHECK(db.findByPath("copy.txt")->missing);
	CHECK(hasText(d3.warnings, "ファイルがありません"));

	// 外部の参照
	writeText(work / "hero.txt", "external");
	Guid id;
	std::string error;
	REQUIRE_MESSAGE(db.linkExternal(work / "hero.txt", "chara/hero.txt", id, error), error);
	CHECK_FALSE(db.linkExternal(work / "hero.txt", "chara/hero.txt", id, error));   // 同じ名前
	CHECK_FALSE(db.linkExternal(work / "none.txt", "x.txt", id, error));            // ない
	db.scan(root, diag);
	const AssetRecord* ext = db.find(id);
	REQUIRE(ext);
	CHECK(ext->meta.external());
	CHECK(ext->path == "chara/hero.txt");
	CHECK_FALSE(ext->missing);
	const fs::path cache = freshDir("sasset_db_external_cache");
	fs::path artifact;
	REQUIRE(db.import(*ext, cache, artifact, diag));
	fs::remove(work / "hero.txt");
	Diagnostics d4;
	db.scan(root, d4);
	CHECK(db.find(id)->missing);
	CHECK(hasText(d4.warnings, "外部のファイルが見つかりません"));

	fs::remove_all(root);
	fs::remove_all(work);
	fs::remove_all(cache);
}
