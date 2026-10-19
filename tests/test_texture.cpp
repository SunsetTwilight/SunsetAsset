// ============================================================
//  テクスチャの取り込みの単体テスト (TGA をテストの中で作る)
// ============================================================
#include <doctest/doctest.h>
#include "sunset/asset/AssetDatabase.h"
#include "sunset/asset/texture/Texture.h"

#include <fstream>

using namespace sunset::asset;
namespace fs = std::filesystem;

namespace
{
// 非圧縮の 32 ビット TGA (左上が原点)。2 x 2: 赤、緑 / 青、白 (半透明)
std::string makeTga()
{
	std::string t(18, '\0');
	t[2]  = 2;                       // 非圧縮の true color
	t[12] = 2; t[14] = 2;            // 幅、高さ
	t[16] = 32;                      // ビット数
	t[17] = 0x28;                    // アルファ 8 ビット、左上が原点
	const unsigned char px[] = {     // BGRA
		0, 0, 255, 255,   0, 255, 0, 255,
		255, 0, 0, 255,   255, 255, 255, 128,
	};
	t.append(reinterpret_cast<const char*>(px), sizeof(px));
	return t;
}
} // namespace

TEST_CASE("テクスチャ: TGA を読み、キャッシュに入れて戻す")
{
	const std::string tga = makeTga();
	Texture t;
	std::string error;
	REQUIRE_MESSAGE(decodeImage(tga.data(), tga.size(), t, &error), error);
	CHECK(t.width == 2);
	CHECK(t.height == 2);
	CHECK(t.channels == 4);
	REQUIRE(t.rgba.size() == 16);
	CHECK(t.rgba[0] == 255);   // 左上は赤
	CHECK(t.rgba[1] == 0);
	CHECK(t.rgba[5] == 255);   // 右上は緑
	CHECK(t.rgba[10] == 255);  // 左下は青
	CHECK(t.rgba[15] == 128);  // 右下のアルファ

	CHECK_FALSE(decodeImage("nope", 4, t, &error));

	// データベース経由 (srgb を false に)
	const fs::path root = fs::temp_directory_path() / "sasset_texture_root";
	const fs::path cache = fs::temp_directory_path() / "sasset_texture_cache";
	std::error_code ec;
	fs::remove_all(root, ec);
	fs::remove_all(cache, ec);
	fs::create_directories(root);
	std::ofstream(root / "normal.tga", std::ios::binary).write(tga.data(), static_cast<std::streamsize>(tga.size()));
	ImporterRegistry reg;
	reg.add(makeTextureImporter());
	AssetDatabase db(reg);
	Diagnostics diag;
	db.scan(root, diag);
	const AssetRecord* r = db.findByPath("normal.tga");
	REQUIRE(r);
	AssetMeta meta = r->meta;
	meta.settings = { { "srgb", false } };
	REQUIRE(writeMeta(r->metaFile, meta));
	db.scan(root, diag);
	fs::path artifact;
	REQUIRE(db.import(*db.findByPath("normal.tga"), cache, artifact, diag));
	Texture back;
	REQUIRE(loadTexture(artifact, back));
	CHECK_FALSE(back.srgb);
	CHECK(back.rgba == t.rgba);
	fs::remove_all(root, ec);
	fs::remove_all(cache, ec);
}
