#pragma once
// ============================================================
//  sunset/asset/texture/Texture.h
//
//  テクスチャの取り込み ("texture"): .png / .jpg / .jpeg / .tga / .bmp (stb_image)。
//  画素は RGBA8 (1 行ずつ上から)。mip の生成と圧縮、HDR (16 ビット / 浮動小数) は後の段階。
//
//  設定 (.meta の settings):
//    srgb  bool  true   色のテクスチャ (sRGB)。法線 / 粗さなどのデータは false にする
// ============================================================

#include "sunset/asset/Importer.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sunset::asset
{
struct Texture
{
	uint32_t             width    = 0;
	uint32_t             height   = 0;
	uint32_t             channels = 4;   // 元のファイルのチャンネルの数 (1〜4)。画素はいつも RGBA8
	bool                 srgb     = true;
	std::vector<uint8_t> rgba;           // width × height × 4
};

std::unique_ptr<Importer> makeTextureImporter();

void writeTexture(const Texture& texture, ArtifactWriter& out);
bool readTexture(const ArtifactReader& in, Texture& out, std::string* error = nullptr);
bool loadTexture(const std::filesystem::path& artifact, Texture& out, std::string* error = nullptr);

// メモリ上の画像 (PNG など) を読む (テストやアプリから直接使う)
bool decodeImage(const void* data, size_t size, Texture& out, std::string* error = nullptr);
} // namespace sunset::asset
