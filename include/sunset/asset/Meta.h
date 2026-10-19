#pragma once
// ============================================================
//  sunset/asset/Meta.h
//
//  .meta: アセットのファイルの隣に置く JSON。ID と取り込みの設定を持つ (git で管理する)。
//
//    { "version": 1, "id": "3f2a…", "importer": "model", "settings": { … },
//      "source": "D:/work/hero.fbx" }        ← 外部の参照のときだけ (.meta だけを assets/ に置く)
// ============================================================

#include "Common.h"
#include "Guid.h"

#include <filesystem>
#include <optional>
#include <string>

namespace sunset::asset
{
constexpr int k_MetaVersion = 1;

struct AssetMeta
{
	int         version = k_MetaVersion;
	Guid        id;
	std::string importer;   // 取り込みの名前 ("model" / "texture" …)
	Json        settings = Json::object();
	std::string source;     // 外部の参照: 元のファイルの絶対パス (UTF-8)。空ならメタの隣のファイル

	bool external() const { return !source.empty(); }
};

// <ファイル>.meta のパス
std::filesystem::path metaPathFor(const std::filesystem::path& asset);
// .meta のパスから、アセットのファイルのパス (末尾の .meta を除く)
std::filesystem::path assetPathForMeta(const std::filesystem::path& meta);
bool isMetaFile(const std::filesystem::path& p);

Json metaToJson(const AssetMeta& meta);
// 形が違えば false (diag に理由)。知らないキーは警告
bool metaFromJson(const Json& json, AssetMeta& out, Diagnostics& diag);

bool readMeta(const std::filesystem::path& metaFile, AssetMeta& out, Diagnostics& diag);
bool writeMeta(const std::filesystem::path& metaFile, const AssetMeta& meta, std::string* error = nullptr);
} // namespace sunset::asset
