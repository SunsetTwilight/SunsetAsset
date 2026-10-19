#pragma once
// ============================================================
//  src/model/MeshBuilder.h (ライブラリの中だけで使う)
//
//  取り込みで共通に使う部品: 頂点をまとめてメッシュに入れる (同じ頂点は 1 つにする)、
//  接線を作る、境界の箱を求める、骨の重みを 4 つにして合計 1 にする。
// ============================================================

#include "sunset/asset/model/Model.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace sunset::asset::detail
{
struct Vertex
{
	float    pos[3]     = {};
	float    normal[3]  = {};
	float    uv0[2]     = {};
	float    uv1[2]     = {};
	float    color[4]   = { 1, 1, 1, 1 };
	uint16_t joints[4]  = {};
	float    weights[4] = {};
};

class MeshBuilder
{
public:
	MeshBuilder(bool uv0, bool uv1, bool color, bool skin) : m_uv0(uv0), m_uv1(uv1), m_color(color), m_skin(skin) {}

	// 頂点を加えて番号を返す。dedupe なら同じ中身の頂点は前のものを使う。source は元のファイルの頂点の番号 (モーフに使う)
	uint32_t add(const Vertex& v, uint32_t source, bool dedupe = true);
	void     index(uint32_t i) { m_indices.push_back(i); }
	uint32_t indexCount() const { return static_cast<uint32_t>(m_indices.size()); }
	const std::vector<uint32_t>& sources() const { return m_sources; }

	// メッシュに入れる (submeshes と morphs は呼び出し側で)
	void finish(ModelMesh& out) const;

private:
	bool                                       m_uv0, m_uv1, m_color, m_skin;
	std::vector<Vertex>                        m_vertices;
	std::vector<uint32_t>                      m_sources;
	std::vector<uint32_t>                      m_indices;
	std::unordered_map<std::string, uint32_t>  m_lookup;
};

// 骨の重み (番号, 重み) の並びから、大きい 4 つを残して合計 1 にする
void pickWeights(std::vector<std::pair<uint32_t, float>>& weights, uint16_t outJoints[4], float outWeights[4]);

// UV0 があるメッシュに接線を作る (三角形ごとに足して、法線に直交させる)
void generateTangents(ModelMesh& mesh);
void computeBounds(ModelMesh& mesh);
// ノードの変換を含めたモデル全体の境界
void computeModelBounds(Model& model);
} // namespace sunset::asset::detail
