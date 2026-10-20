#pragma once
// ============================================================
//  src/model/ModelFormats.h (ライブラリの中だけで使う)
//
//  形式ごとの取り込み。どれもモデルの型 (右手系、Y が上、メートル、UV は左上が原点) にして返す。
// ============================================================

#include "sunset/asset/Common.h"
#include "sunset/asset/model/Model.h"

#include <filesystem>
#include <string>
#include <vector>

namespace sunset::asset::detail
{
bool importGltf(const std::filesystem::path& file, Model& out, Diagnostics& diag);
bool importUfbx(const std::filesystem::path& file, Model& out, Diagnostics& diag);   // FBX と OBJ
// PMX / PMD (MMD)。scale は設定の大きさ (MMD の単位 0.08 m に掛ける。剛体などの extras にも掛ける)
bool importMmd(const std::filesystem::path& file, Model& out, Diagnostics& diag, float scale);

// 元のファイルが参照しているファイル (テクスチャ、glTF の .bin など)。元のファイルからの相対パス (区切りは "/")
std::vector<std::string> gltfReferencedFiles(const std::filesystem::path& file, Diagnostics& diag);
std::vector<std::string> ufbxReferencedFiles(const std::filesystem::path& file, Diagnostics& diag);
std::vector<std::string> mmdReferencedFiles(const std::filesystem::path& file, Diagnostics& diag);   // テクスチャ / スフィア / 個別のトゥーン
} // namespace sunset::asset::detail
