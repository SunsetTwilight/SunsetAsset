// ============================================================
//  src/model/MeshBuilder.cpp
// ============================================================
#include "MeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace sunset::asset::detail
{
uint32_t MeshBuilder::add(const Vertex& v, uint32_t source, bool dedupe)
{
	if (dedupe)
	{
		std::string key(reinterpret_cast<const char*>(&v), sizeof(Vertex));
		key.append(reinterpret_cast<const char*>(&source), sizeof(source));   // 元の頂点が違えばモーフも違うので分ける
		const auto it = m_lookup.find(key);
		if (it != m_lookup.end())
			return it->second;
		const uint32_t index = static_cast<uint32_t>(m_vertices.size());
		m_lookup.emplace(std::move(key), index);
		m_vertices.push_back(v);
		m_sources.push_back(source);
		return index;
	}
	m_vertices.push_back(v);
	m_sources.push_back(source);
	return static_cast<uint32_t>(m_vertices.size() - 1);
}

void MeshBuilder::finish(ModelMesh& out) const
{
	const size_t n = m_vertices.size();
	out.vertexCount = static_cast<uint32_t>(n);
	out.positions.resize(n * 3);
	out.normals.resize(n * 3);
	if (m_uv0) out.uv0.resize(n * 2);
	if (m_uv1) out.uv1.resize(n * 2);
	if (m_color) out.colors.resize(n * 4);
	if (m_skin) { out.joints.resize(n * 4); out.weights.resize(n * 4); }
	for (size_t i = 0; i < n; ++i)
	{
		const Vertex& v = m_vertices[i];
		std::memcpy(&out.positions[i * 3], v.pos, sizeof(v.pos));
		std::memcpy(&out.normals[i * 3], v.normal, sizeof(v.normal));
		if (m_uv0) std::memcpy(&out.uv0[i * 2], v.uv0, sizeof(v.uv0));
		if (m_uv1) std::memcpy(&out.uv1[i * 2], v.uv1, sizeof(v.uv1));
		if (m_color) std::memcpy(&out.colors[i * 4], v.color, sizeof(v.color));
		if (m_skin)
		{
			std::memcpy(&out.joints[i * 4], v.joints, sizeof(v.joints));
			std::memcpy(&out.weights[i * 4], v.weights, sizeof(v.weights));
		}
	}
	out.indices = m_indices;
}

void pickWeights(std::vector<std::pair<uint32_t, float>>& w, uint16_t outJoints[4], float outWeights[4])
{
	std::sort(w.begin(), w.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
	float sum = 0;
	for (size_t i = 0; i < 4; ++i)
	{
		outJoints[i]  = i < w.size() ? static_cast<uint16_t>(w[i].first) : 0;
		outWeights[i] = i < w.size() ? std::max(0.0f, w[i].second) : 0.0f;
		sum += outWeights[i];
	}
	if (sum > 0)
		for (size_t i = 0; i < 4; ++i) outWeights[i] /= sum;
	else
		outWeights[0] = 1;   // 重みのない頂点は最初の骨に付ける
}

void generateTangents(ModelMesh& m)
{
	if (m.uv0.empty() || m.vertexCount == 0)
		return;
	const size_t n = m.vertexCount;
	std::vector<float> tan(n * 3, 0.0f), bit(n * 3, 0.0f);
	for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
	{
		const uint32_t i0 = m.indices[t], i1 = m.indices[t + 1], i2 = m.indices[t + 2];
		if (i0 >= n || i1 >= n || i2 >= n) continue;
		const float* p0 = &m.positions[i0 * 3]; const float* p1 = &m.positions[i1 * 3]; const float* p2 = &m.positions[i2 * 3];
		const float* w0 = &m.uv0[i0 * 2];       const float* w1 = &m.uv0[i1 * 2];       const float* w2 = &m.uv0[i2 * 2];
		const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
		const float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
		const float du1 = w1[0] - w0[0], dv1 = w1[1] - w0[1], du2 = w2[0] - w0[0], dv2 = w2[1] - w0[1];
		const float det = du1 * dv2 - du2 * dv1;
		if (std::fabs(det) < 1e-12f) continue;
		const float r = 1.0f / det;
		for (int k = 0; k < 3; ++k)
		{
			const float tk = (e1[k] * dv2 - e2[k] * dv1) * r;
			const float bk = (e2[k] * du1 - e1[k] * du2) * r;
			for (uint32_t idx : { i0, i1, i2 })
			{
				tan[idx * 3 + static_cast<size_t>(k)] += tk;
				bit[idx * 3 + static_cast<size_t>(k)] += bk;
			}
		}
	}
	m.tangents.assign(n * 4, 0.0f);
	for (size_t i = 0; i < n; ++i)
	{
		const float* nn = &m.normals[i * 3];
		float t[3] = { tan[i * 3], tan[i * 3 + 1], tan[i * 3 + 2] };
		// 法線に直交させる (グラム・シュミット)
		const float d = t[0] * nn[0] + t[1] * nn[1] + t[2] * nn[2];
		for (int k = 0; k < 3; ++k) t[k] -= nn[k] * d;
		float len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
		if (len < 1e-12f)
		{
			// UV が潰れている: 法線に直交する適当な向き
			const float a[3] = { std::fabs(nn[0]) < 0.9f ? 1.0f : 0.0f, std::fabs(nn[0]) < 0.9f ? 0.0f : 1.0f, 0.0f };
			t[0] = a[1] * nn[2] - a[2] * nn[1]; t[1] = a[2] * nn[0] - a[0] * nn[2]; t[2] = a[0] * nn[1] - a[1] * nn[0];
			len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
		}
		for (int k = 0; k < 3; ++k) m.tangents[i * 4 + static_cast<size_t>(k)] = len > 0 ? t[k] / len : 0;
		// 従法線の向き: (n × t) · b
		const float c[3] = { nn[1] * t[2] - nn[2] * t[1], nn[2] * t[0] - nn[0] * t[2], nn[0] * t[1] - nn[1] * t[0] };
		const float hand = c[0] * bit[i * 3] + c[1] * bit[i * 3 + 1] + c[2] * bit[i * 3 + 2];
		m.tangents[i * 4 + 3] = hand < 0 ? -1.0f : 1.0f;
	}
}

void computeBounds(ModelMesh& m)
{
	if (m.vertexCount == 0)
	{
		m.boundsMin = m.boundsMax = { 0, 0, 0 };
		return;
	}
	m.boundsMin = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
	m.boundsMax = { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
	for (size_t i = 0; i < m.vertexCount; ++i)
		for (size_t k = 0; k < 3; ++k)
		{
			m.boundsMin[k] = std::min(m.boundsMin[k], m.positions[i * 3 + k]);
			m.boundsMax[k] = std::max(m.boundsMax[k], m.positions[i * 3 + k]);
		}
}

void computeModelBounds(Model& model)
{
	const auto world = nodeWorldMatrices(model);
	bool any = false;
	Float3 lo {}, hi {};
	for (size_t i = 0; i < model.nodes.size(); ++i)
	{
		const int32_t mi = model.nodes[i].mesh;
		if (mi < 0 || mi >= static_cast<int32_t>(model.meshes.size())) continue;
		const ModelMesh& mesh = model.meshes[static_cast<size_t>(mi)];
		if (mesh.vertexCount == 0) continue;
		const auto& w = world[i];
		for (int c = 0; c < 8; ++c)
		{
			const float p[3] = { (c & 1) ? mesh.boundsMax[0] : mesh.boundsMin[0], (c & 2) ? mesh.boundsMax[1] : mesh.boundsMin[1],
			                     (c & 4) ? mesh.boundsMax[2] : mesh.boundsMin[2] };
			for (size_t k = 0; k < 3; ++k)
			{
				const float v = w[k] * p[0] + w[4 + k] * p[1] + w[8 + k] * p[2] + w[12 + k];
				lo[k] = any ? std::min(lo[k], v) : v;
				hi[k] = any ? std::max(hi[k], v) : v;
			}
			any = true;
		}
	}
	model.boundsMin = lo;
	model.boundsMax = hi;
}
} // namespace sunset::asset::detail
