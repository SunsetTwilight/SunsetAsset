// ============================================================
//  src/model/UfbxImport.cpp
//
//  FBX / OBJ の取り込み (ufbx)。
//    ・単位と軸は ufbx に変換させる (右手系、Y が上、メートル。Maya の cm → m)。ジオメトリの変換もメッシュに焼き込む
//    ・面は三角形に分け、同じ頂点は 1 つにまとめる。マテリアルごとにサブメッシュを作る
//    ・UV は FBX (左下が原点) から glTF と同じ (左上が原点) にする
//    ・スキン: 最初のスキンのクラスタを骨にし、頂点ごとに重みの大きい 4 つを残す。逆バインド行列は geometry_to_bone
//    ・ブレンドシェイプ: チャンネルの最後のキーフレームの形をモーフにする
// ============================================================
#include "ModelFormats.h"
#include "MeshBuilder.h"

#include <ufbx.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace sunset::asset::detail
{
namespace fs = std::filesystem;

namespace
{
struct SceneScope
{
	ufbx_scene* scene = nullptr;
	~SceneScope() { if (scene) ufbx_free_scene(scene); }
};

std::string str(const ufbx_string& s) { return std::string(s.data, s.length); }

ufbx_scene* load(const fs::path& file, Diagnostics& diag)
{
	ufbx_load_opts opts = {};
	opts.target_axes                 = ufbx_axes_right_handed_y_up;
	opts.target_unit_meters          = 1.0;
	opts.space_conversion            = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
	opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
	opts.generate_missing_normals    = true;
	ufbx_error error = {};
	const std::string path = pathToUtf8(file);   // ufbx は UTF-8 のパスを受け取る (Windows では UTF-16 にして開く)
	ufbx_scene* scene = ufbx_load_file_len(path.data(), path.size(), &opts, &error);
	if (!scene)
	{
		char buf[512];
		ufbx_format_error(buf, sizeof(buf), &error);
		diag.error(std::string("FBX / OBJ を読めません: ") + buf);
	}
	return scene;
}

void toMatrix(const ufbx_matrix& m, float out[16])
{
	for (int c = 0; c < 4; ++c)
		for (int r = 0; r < 3; ++r)
			out[c * 4 + r] = static_cast<float>(m.cols[c].v[r]);
	out[3] = out[7] = out[11] = 0;
	out[15] = 1;
}

TextureRef texRef(const ufbx_material_map& map)
{
	TextureRef t;
	if (map.texture && map.texture_enabled != false)
	{
		const ufbx_texture* tex = map.texture;
		std::string p = str(tex->relative_filename);
		if (p.empty()) p = str(tex->filename);
		std::replace(p.begin(), p.end(), '\\', '/');
		t.path = p;
	}
	return t;
}

float real(const ufbx_material_map& m, float fallback) { return m.has_value ? static_cast<float>(m.value_real) : fallback; }
} // namespace

bool importUfbx(const fs::path& file, Model& out, Diagnostics& diag)
{
	SceneScope scope;
	scope.scene = load(file, diag);
	if (!scope.scene)
		return false;
	const ufbx_scene* sc = scope.scene;
	Model model;

	// ---- スロット ----
	for (size_t i = 0; i < sc->materials.count; ++i)
	{
		const ufbx_material* m = sc->materials.data[i];
		MaterialSlot s;
		s.name = str(m->name);
		if (s.name.empty()) s.name = "Material" + std::to_string(i);
		const auto& pbr = m->pbr;
		if (pbr.base_color.has_value)
		{
			const float f = real(pbr.base_factor, 1.0f);
			s.baseColor = { static_cast<float>(pbr.base_color.value_vec4.x) * f, static_cast<float>(pbr.base_color.value_vec4.y) * f,
			                static_cast<float>(pbr.base_color.value_vec4.z) * f, 1.0f };
		}
		s.metallic  = real(pbr.metalness, 0.0f);
		s.roughness = real(pbr.roughness, 0.5f);
		if (pbr.emission_color.has_value)
		{
			const float f = real(pbr.emission_factor, 1.0f);
			s.emissive = { static_cast<float>(pbr.emission_color.value_vec3.x) * f, static_cast<float>(pbr.emission_color.value_vec3.y) * f,
			               static_cast<float>(pbr.emission_color.value_vec3.z) * f };
		}
		const float opacity = real(pbr.opacity, 1.0f);
		s.baseColor[3] = opacity;
		if (opacity < 0.999f || pbr.opacity.texture)
			s.alphaMode = AlphaMode::Blend;
		s.baseColorTexture         = texRef(pbr.base_color);
		s.normalTexture            = texRef(pbr.normal_map);
		s.metallicRoughnessTexture = texRef(pbr.roughness);
		s.occlusionTexture         = texRef(pbr.ambient_occlusion);
		s.emissiveTexture          = texRef(pbr.emission_color);
		model.slots.push_back(s);
	}
	int32_t defaultSlot = -1;
	auto fallbackSlot = [&]() -> uint32_t {
		if (defaultSlot < 0)
		{
			defaultSlot = static_cast<int32_t>(model.slots.size());
			MaterialSlot s;
			s.name = "Default";
			model.slots.push_back(s);
		}
		return static_cast<uint32_t>(defaultSlot);
	};

	// ---- ノード (ufbx の並びのまま。親は typed_id) ----
	for (size_t i = 0; i < sc->nodes.count; ++i)
	{
		const ufbx_node* n = sc->nodes.data[i];
		ModelNode node;
		node.name = n->is_root ? "Root" : str(n->name);
		node.parent = n->parent ? static_cast<int32_t>(n->parent->typed_id) : -1;
		const ufbx_transform& t = n->local_transform;
		node.translation = { static_cast<float>(t.translation.x), static_cast<float>(t.translation.y), static_cast<float>(t.translation.z) };
		node.rotation    = { static_cast<float>(t.rotation.x), static_cast<float>(t.rotation.y), static_cast<float>(t.rotation.z), static_cast<float>(t.rotation.w) };
		node.scale       = { static_cast<float>(t.scale.x), static_cast<float>(t.scale.y), static_cast<float>(t.scale.z) };
		node.mesh = n->mesh ? static_cast<int32_t>(n->mesh->typed_id) : -1;
		model.nodes.push_back(node);
	}

	// ---- メッシュ ----
	std::vector<uint32_t> tri;
	for (size_t mi = 0; mi < sc->meshes.count; ++mi)
	{
		const ufbx_mesh* m = sc->meshes.data[mi];
		const ufbx_skin_deformer* skin = m->skin_deformers.count ? m->skin_deformers.data[0] : nullptr;
		const bool hasUv0 = m->vertex_uv.exists;
		const bool hasUv1 = m->uv_sets.count > 1 && m->uv_sets.data[1].vertex_uv.exists;
		const bool hasColor = m->vertex_color.exists;
		MeshBuilder mb(hasUv0, hasUv1, hasColor, skin != nullptr);
		ModelMesh mesh;
		mesh.name = str(m->name);
		if (mesh.name.empty() && m->instances.count) mesh.name = str(m->instances.data[0]->name);

		// スキン: クラスタ = 骨。頂点ごとの重みは先に作っておく
		int32_t skinIndex = -1;
		std::vector<std::pair<std::array<uint16_t, 4>, std::array<float, 4>>> vertexSkin;
		if (skin)
		{
			ModelSkin s;
			s.name = str(skin->name);
			for (size_t c = 0; c < skin->clusters.count; ++c)
			{
				const ufbx_skin_cluster* cl = skin->clusters.data[c];
				s.joints.push_back(cl->bone_node ? static_cast<int32_t>(cl->bone_node->typed_id) : -1);
				float ib[16];
				toMatrix(cl->geometry_to_bone, ib);
				s.inverseBind.insert(s.inverseBind.end(), ib, ib + 16);
			}
			skinIndex = static_cast<int32_t>(model.skins.size());
			model.skins.push_back(std::move(s));
			vertexSkin.resize(m->num_vertices);
			for (size_t v = 0; v < m->num_vertices && v < skin->vertices.count; ++v)
			{
				const ufbx_skin_vertex& sv = skin->vertices.data[v];
				std::vector<std::pair<uint32_t, float>> list;
				for (uint32_t k = 0; k < sv.num_weights; ++k)
				{
					const ufbx_skin_weight& w = skin->weights.data[sv.weight_begin + k];
					list.emplace_back(w.cluster_index, static_cast<float>(w.weight));
				}
				pickWeights(list, vertexSkin[v].first.data(), vertexSkin[v].second.data());
			}
		}

		auto corner = [&](uint32_t idx) {
			Vertex vx;
			const ufbx_vec3 p = m->vertex_position.values.data[m->vertex_position.indices.data[idx]];
			vx.pos[0] = static_cast<float>(p.x); vx.pos[1] = static_cast<float>(p.y); vx.pos[2] = static_cast<float>(p.z);
			if (m->vertex_normal.exists)
			{
				const ufbx_vec3 n = m->vertex_normal.values.data[m->vertex_normal.indices.data[idx]];
				vx.normal[0] = static_cast<float>(n.x); vx.normal[1] = static_cast<float>(n.y); vx.normal[2] = static_cast<float>(n.z);
			}
			if (hasUv0)
			{
				const ufbx_vec2 uv = m->vertex_uv.values.data[m->vertex_uv.indices.data[idx]];
				vx.uv0[0] = static_cast<float>(uv.x); vx.uv0[1] = 1.0f - static_cast<float>(uv.y);
			}
			if (hasUv1)
			{
				const ufbx_vertex_vec2& a = m->uv_sets.data[1].vertex_uv;
				const ufbx_vec2 uv = a.values.data[a.indices.data[idx]];
				vx.uv1[0] = static_cast<float>(uv.x); vx.uv1[1] = 1.0f - static_cast<float>(uv.y);
			}
			if (hasColor)
			{
				const ufbx_vec4 c = m->vertex_color.values.data[m->vertex_color.indices.data[idx]];
				vx.color[0] = static_cast<float>(c.x); vx.color[1] = static_cast<float>(c.y); vx.color[2] = static_cast<float>(c.z); vx.color[3] = static_cast<float>(c.w);
			}
			const uint32_t logical = m->vertex_indices.data[idx];
			if (skin && logical < vertexSkin.size())
			{
				std::copy(vertexSkin[logical].first.begin(), vertexSkin[logical].first.end(), vx.joints);
				std::copy(vertexSkin[logical].second.begin(), vertexSkin[logical].second.end(), vx.weights);
			}
			return mb.add(vx, logical);
		};

		tri.resize(m->max_face_triangles * 3);
		auto emitFaces = [&](const uint32_t* faces, size_t count, uint32_t slot) {
			SubMesh sm;
			sm.indexOffset = mb.indexCount();
			for (size_t f = 0; f < count; ++f)
			{
				const ufbx_face face = m->faces.data[faces ? faces[f] : f];
				if (face.num_indices < 3) continue;
				const uint32_t nt = ufbx_triangulate_face(tri.data(), tri.size(), m, face);
				for (uint32_t t = 0; t < nt; ++t)
				{
					uint32_t a = tri[t * 3], b = tri[t * 3 + 1], c = tri[t * 3 + 2];
					if (m->reversed_winding) std::swap(b, c);
					mb.index(corner(a));
					mb.index(corner(b));
					mb.index(corner(c));
				}
			}
			sm.indexCount = mb.indexCount() - sm.indexOffset;
			sm.slot       = slot;
			if (sm.indexCount)
				mesh.submeshes.push_back(sm);
		};
		if (m->material_parts.count)
			for (size_t p = 0; p < m->material_parts.count; ++p)
			{
				const ufbx_mesh_part& part = m->material_parts.data[p];
				const ufbx_material* mat = part.index < m->materials.count ? m->materials.data[part.index] : nullptr;
				emitFaces(part.face_indices.data, part.face_indices.count, mat ? mat->typed_id : fallbackSlot());
			}
		else
			emitFaces(nullptr, m->faces.count, fallbackSlot());
		mb.finish(mesh);

		// ---- ブレンドシェイプ → モーフ (元の頂点ごとの差分を、まとめた頂点に配る) ----
		for (size_t bd = 0; bd < m->blend_deformers.count; ++bd)
			for (size_t ch = 0; ch < m->blend_deformers.data[bd]->channels.count; ++ch)
			{
				const ufbx_blend_channel* channel = m->blend_deformers.data[bd]->channels.data[ch];
				const ufbx_blend_shape* shape = channel->target_shape ? channel->target_shape
				                              : channel->keyframes.count ? channel->keyframes.data[channel->keyframes.count - 1].shape : nullptr;
				if (!shape) continue;
				std::unordered_map<uint32_t, size_t> offsetOf;
				for (size_t k = 0; k < shape->num_offsets; ++k)
					offsetOf.emplace(shape->offset_vertices.data[k], k);
				MorphTarget mt;
				mt.name = str(channel->name);
				mt.defaultWeight = static_cast<float>(channel->weight);
				mt.positions.assign(static_cast<size_t>(mesh.vertexCount) * 3, 0.0f);
				const bool normals = shape->normal_offsets.count == shape->num_offsets && shape->num_offsets > 0;
				if (normals) mt.normals.assign(static_cast<size_t>(mesh.vertexCount) * 3, 0.0f);
				for (size_t v = 0; v < mesh.vertexCount; ++v)
				{
					const auto it = offsetOf.find(mb.sources()[v]);
					if (it == offsetOf.end()) continue;
					const ufbx_vec3 d = shape->position_offsets.data[it->second];
					mt.positions[v * 3] = static_cast<float>(d.x); mt.positions[v * 3 + 1] = static_cast<float>(d.y); mt.positions[v * 3 + 2] = static_cast<float>(d.z);
					if (normals)
					{
						const ufbx_vec3 nn = shape->normal_offsets.data[it->second];
						mt.normals[v * 3] = static_cast<float>(nn.x); mt.normals[v * 3 + 1] = static_cast<float>(nn.y); mt.normals[v * 3 + 2] = static_cast<float>(nn.z);
					}
				}
				mesh.morphs.push_back(std::move(mt));
			}

		generateTangents(mesh);
		computeBounds(mesh);
		model.meshes.push_back(std::move(mesh));
		// スキンをメッシュの持ち主のノードに付ける
		if (skinIndex >= 0)
			for (size_t inst = 0; inst < m->instances.count; ++inst)
				model.nodes[m->instances.data[inst]->typed_id].skin = skinIndex;
	}

	computeModelBounds(model);
	out = std::move(model);
	return true;
}

std::vector<std::string> ufbxReferencedFiles(const fs::path& file, Diagnostics& diag)
{
	std::vector<std::string> out;
	SceneScope scope;
	scope.scene = load(file, diag);
	if (!scope.scene)
		return out;
	for (size_t i = 0; i < scope.scene->textures.count; ++i)
	{
		const ufbx_texture* t = scope.scene->textures.data[i];
		if (t->content.size > 0) continue;   // 埋め込み
		std::string p = str(t->relative_filename);
		if (p.empty()) p = str(t->filename);
		std::replace(p.begin(), p.end(), '\\', '/');
		if (!p.empty() && std::find(out.begin(), out.end(), p) == out.end())
			out.push_back(p);
	}
	return out;
}
} // namespace sunset::asset::detail
