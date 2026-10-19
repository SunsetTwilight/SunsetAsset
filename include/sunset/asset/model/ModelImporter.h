#pragma once
// ============================================================
//  sunset/asset/model/ModelImporter.h
//
//  モデルの取り込み ("model"): .fbx / .obj (ufbx)、.gltf / .glb (cgltf)。結果はモデルの型 (Model.h) をキャッシュに入れる。
//
//  設定 (.meta の settings):
//    scale             float  1.0   さらに掛ける大きさ (単位の変換は自動。FBX の cm → m など)
//    generateTangents  bool   true  UV0 があるメッシュに接線を作る
// ============================================================

#include "sunset/asset/Importer.h"
#include "sunset/asset/model/Model.h"

#include <filesystem>
#include <memory>
#include <string>

namespace sunset::asset
{
std::unique_ptr<Importer> makeModelImporter();

// 取り込みのキャッシュ (AssetDatabase::import が書いたもの) からモデルを読む
bool loadModel(const std::filesystem::path& artifact, Model& out, std::string* error = nullptr);
} // namespace sunset::asset
