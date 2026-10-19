#pragma once
// ============================================================
//  sunset/asset/model/Model.h
//
//  取り込んだモデル (FBX / glTF / OBJ から共通の形に変換したもの)。GPU の形にはしない (使う側が決める)。
//
//  座標は右手系、Y が上、単位はメートル。回転はクォータニオン (x, y, z, w)。行列は列優先 (m[列 * 4 + 行])。
//  三角形の表は反時計回り (glTF と同じ)。
//
//    ノード   部品の階層 (親、ローカルの位置 / 回転 / 大きさ、メッシュ、スキン)
//    メッシュ 頂点の列 (位置、法線、接線、UV0 / UV1、色、骨の番号と重み)、インデックス、サブメッシュ、モーフ、境界の箱
//    スロット マテリアルのスロット (名前と、取り込んだマテリアルの情報)。サブメッシュはスロットの番号を持つ
//    スキン   骨 (ノードの番号) と逆バインド行列
// ============================================================

#include "sunset/asset/Artifact.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sunset::asset
{
using Float3 = std::array<float, 3>;
using Float4 = std::array<float, 4>;

struct ModelNode
{
	std::string name;
	int32_t     parent = -1;
	Float3      translation { 0, 0, 0 };
	Float4      rotation { 0, 0, 0, 1 };   // クォータニオン (x, y, z, w)
	Float3      scale { 1, 1, 1 };
	int32_t     mesh = -1;
	int32_t     skin = -1;
};

struct SubMesh
{
	uint32_t indexOffset = 0;
	uint32_t indexCount  = 0;
	uint32_t slot        = 0;   // Model::slots の番号
};

struct MorphTarget
{
	std::string        name;
	float              defaultWeight = 0;
	std::vector<float> positions;   // 頂点ごとの差分 (3 × 頂点の数)
	std::vector<float> normals;     // 同 (なければ空)
};

struct ModelMesh
{
	std::string           name;
	uint32_t              vertexCount = 0;
	std::vector<float>    positions;   // 3 × 頂点の数
	std::vector<float>    normals;     // 3 ×
	std::vector<float>    tangents;    // 4 × (w は従法線の向き ±1)。UV がなければ空
	std::vector<float>    uv0, uv1;    // 2 × (なければ空)
	std::vector<float>    colors;      // 4 × (なければ空)
	std::vector<uint16_t> joints;      // 4 × スキンの骨の番号 (Skin::joints の番号。なければ空)
	std::vector<float>    weights;     // 4 × (合計 1)
	std::vector<uint32_t> indices;
	std::vector<SubMesh>  submeshes;
	std::vector<MorphTarget> morphs;
	Float3                boundsMin { 0, 0, 0 }, boundsMax { 0, 0, 0 };   // メッシュの座標 (バインドポーズ)
};

struct TextureRef
{
	std::string path;   // 元のファイルからの相対パス (区切りは "/")。埋め込みや不明なら空
	int32_t     uvSet = 0;
	bool used() const { return !path.empty(); }
};

enum class AlphaMode : uint8_t { Opaque, Mask, Blend };

struct MaterialSlot
{
	std::string name;
	Float4      baseColor { 1, 1, 1, 1 };
	float       metallic  = 0;
	float       roughness = 1;
	Float3      emissive { 0, 0, 0 };
	AlphaMode   alphaMode   = AlphaMode::Opaque;
	float       alphaCutoff = 0.5f;
	bool        doubleSided = false;
	TextureRef  baseColorTexture, normalTexture, metallicRoughnessTexture, occlusionTexture, emissiveTexture;
};

struct ModelSkin
{
	std::string          name;
	std::vector<int32_t> joints;        // ノードの番号
	std::vector<float>   inverseBind;   // 16 × 骨の数 (列優先)
};

struct Model
{
	std::vector<ModelNode>    nodes;
	std::vector<ModelMesh>    meshes;
	std::vector<MaterialSlot> slots;
	std::vector<ModelSkin>    skins;
	Float3                    boundsMin { 0, 0, 0 }, boundsMax { 0, 0, 0 };   // 全体 (ノードの変換を含む、バインドポーズ)
};

// キャッシュのチャンクとの変換。構造は "MJSN" (JSON)、配列は "MBIN" (バイナリ) に入れる
void writeModel(const Model& model, ArtifactWriter& out);
bool readModel(const ArtifactReader& in, Model& out, std::string* error = nullptr);

// ノードのワールド行列 (列優先 16 個 × ノードの数)。親を掛ける
std::vector<std::array<float, 16>> nodeWorldMatrices(const Model& model);
} // namespace sunset::asset
