// ============================================================
//  src/model/GltfImport.cpp
//
//  glTF 2.0 / GLB の取り込み (cgltf)。座標系 (右手系、Y が上、メートル) はモデルの型と同じなので変換しない。
//  メッシュの primitive をつなげて 1 つのメッシュにし、primitive ごとにサブメッシュを作る (スロット = マテリアルの番号)。
//  ファイルは自前の読み込み (UTF-8 のパス) で cgltf に渡す (Windows の日本語のパスのため)。
// ============================================================
#include "ModelFormats.h"
#include "MeshBuilder.h"

#include <cgltf.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

namespace sunset::asset::detail
{
namespace fs = std::filesystem;

namespace
{
cgltf_result readCallback(const cgltf_memory_options*, const cgltf_file_options*, const char* path, cgltf_size* size, void** data)
{
	std::string bytes;
	if (!readFile(pathFromUtf8(path), bytes))
		return cgltf_result_file_not_found;
	void* mem = std::malloc(bytes.size() ? bytes.size() : 1);
	if (!mem)
		return cgltf_result_out_of_memory;
	std::memcpy(mem, bytes.data(), bytes.size());
	*size = bytes.size();
	*data = mem;
	return cgltf_result_success;
}

void releaseCallback(const cgltf_memory_options*, const cgltf_file_options*, void* data)
{
	std::free(data);
}

const char* resultText(cgltf_result r)
{
	switch (r)
	{
	case cgltf_result_data_too_short:   return "データが短すぎます";
	case cgltf_result_unknown_format:   return "glTF ではありません";
	case cgltf_result_invalid_json:     return "JSON が正しくありません";
	case cgltf_result_invalid_gltf:     return "glTF の形が正しくありません";
	case cgltf_result_invalid_options:  return "オプションが正しくありません";
	case cgltf_result_file_not_found:   return "参照しているファイルがありません";
	case cgltf_result_io_error:         return "読み込みに失敗しました";
	case cgltf_result_out_of_memory:    return "メモリが足りません";
	case cgltf_result_legacy_gltf:      return "glTF 1.0 は扱えません (2.0 にしてください)";
	default:                            return "読めません";
	}
}

struct Scope
{
	cgltf_data* data = nullptr;
	~Scope() { if (data) cgltf_free(data); }
};

std::string imageUri(const cgltf_texture_view& v)
{
	if (!v.texture || !v.texture->image || !v.texture->image->uri)
		return {};
	std::string uri = v.texture->image->uri;
	if (uri.rfind("data:", 0) == 0)
		return {};   // 埋め込み (今は扱わない)
	std::vector<char> buf(uri.begin(), uri.end());
	buf.push_back('\0');
	cgltf_decode_uri(buf.data());
	return std::string(buf.data());
}

TextureRef texRef(const cgltf_texture_view& v)
{
	TextureRef t;
	t.path  = imageUri(v);
	t.uvSet = v.texcoord;
	return t;
}

// 行列 (列優先) から位置 / 回転 / 大きさ (せん断は無視する)
void decompose(const float m[16], ModelNode& n)
{
	n.translation = { m[12], m[13], m[14] };
	float s[3];
	for (int c = 0; c < 3; ++c)
		s[c] = std::sqrt(m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] + m[c * 4 + 2] * m[c * 4 + 2]);
	const float det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[4] * (m[1] * m[10] - m[2] * m[9]) + m[8] * (m[1] * m[6] - m[2] * m[5]);
	if (det < 0) s[0] = -s[0];
	n.scale = { s[0], s[1], s[2] };
	float r[9];
	for (int c = 0; c < 3; ++c)
		for (int row = 0; row < 3; ++row)
			r[c * 3 + row] = s[c] != 0 ? m[c * 4 + row] / s[c] : 0;
	// 回転行列 → クォータニオン (r は列優先: r[列 * 3 + 行])
	auto R = [&](int row, int col) { return r[col * 3 + row]; };
	const float tr = R(0, 0) + R(1, 1) + R(2, 2);
	float q[4];
	if (tr > 0)
	{
		const float k = 0.5f / std::sqrt(tr + 1.0f);
		q[3] = 0.25f / k; q[0] = (R(2, 1) - R(1, 2)) * k; q[1] = (R(0, 2) - R(2, 0)) * k; q[2] = (R(1, 0) - R(0, 1)) * k;
	}
	else if (R(0, 0) > R(1, 1) && R(0, 0) > R(2, 2))
	{
		const float k = 2.0f * std::sqrt(1.0f + R(0, 0) - R(1, 1) - R(2, 2));
		q[3] = (R(2, 1) - R(1, 2)) / k; q[0] = 0.25f * k; q[1] = (R(0, 1) + R(1, 0)) / k; q[2] = (R(0, 2) + R(2, 0)) / k;
	}
	else if (R(1, 1) > R(2, 2))
	{
		const float k = 2.0f * std::sqrt(1.0f + R(1, 1) - R(0, 0) - R(2, 2));
		q[3] = (R(0, 2) - R(2, 0)) / k; q[0] = (R(0, 1) + R(1, 0)) / k; q[1] = 0.25f * k; q[2] = (R(1, 2) + R(2, 1)) / k;
	}
	else
	{
		const float k = 2.0f * std::sqrt(1.0f + R(2, 2) - R(0, 0) - R(1, 1));
		q[3] = (R(1, 0) - R(0, 1)) / k; q[0] = (R(0, 2) + R(2, 0)) / k; q[1] = (R(1, 2) + R(2, 1)) / k; q[2] = 0.25f * k;
	}
	n.rotation = { q[0], q[1], q[2], q[3] };
}

bool parse(const fs::path& file, Scope& scope, cgltf_options& options, Diagnostics& diag)
{
	std::memset(&options, 0, sizeof(options));
	options.file.read    = readCallback;
	options.file.release = releaseCallback;
	std::string bytes;
	if (!readFile(file, bytes))
		return diag.error("読めません"), false;
	cgltf_result r = cgltf_parse(&options, bytes.data(), bytes.size(), &scope.data);
	if (r != cgltf_result_success)
		return diag.error(std::string("glTF: ") + resultText(r)), false;
	r = cgltf_load_buffers(&options, scope.data, pathToUtf8(file).c_str());
	if (r != cgltf_result_success)
		return diag.error(std::string("glTF のバッファ: ") + resultText(r)), false;
	r = cgltf_validate(scope.data);
	if (r != cgltf_result_success)
		return diag.error(std::string("glTF の検証: ") + resultText(r)), false;
	return true;
}
} // namespace

bool importGltf(const fs::path& file, Model& out, Diagnostics& diag)
{
	Scope scope;
	cgltf_options options;
	if (!parse(file, scope, options, diag))
		return false;
	const cgltf_data* d = scope.data;
	Model model;

	// ---- スロット (マテリアル)。マテリアルのない primitive 用に、最後に既定のスロットを足すことがある ----
	for (cgltf_size i = 0; i < d->materials_count; ++i)
	{
		const cgltf_material& m = d->materials[i];
		MaterialSlot s;
		s.name = m.name ? m.name : "Material" + std::to_string(i);
		if (m.has_pbr_metallic_roughness)
		{
			const auto& p = m.pbr_metallic_roughness;
			s.baseColor = { p.base_color_factor[0], p.base_color_factor[1], p.base_color_factor[2], p.base_color_factor[3] };
			s.metallic  = p.metallic_factor;
			s.roughness = p.roughness_factor;
			s.baseColorTexture         = texRef(p.base_color_texture);
			s.metallicRoughnessTexture = texRef(p.metallic_roughness_texture);
		}
		s.emissive         = { m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2] };
		s.normalTexture    = texRef(m.normal_texture);
		s.occlusionTexture = texRef(m.occlusion_texture);
		s.emissiveTexture  = texRef(m.emissive_texture);
		s.alphaMode   = m.alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Mask : m.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend : AlphaMode::Opaque;
		s.alphaCutoff = m.alpha_cutoff;
		s.doubleSided = m.double_sided;
		model.slots.push_back(s);
	}
	int32_t defaultSlot = -1;
	auto slotOf = [&](const cgltf_material* m) -> uint32_t {
		if (m) return static_cast<uint32_t>(m - d->materials);
		if (defaultSlot < 0)
		{
			defaultSlot = static_cast<int32_t>(model.slots.size());
			MaterialSlot s;
			s.name = "Default";
			model.slots.push_back(s);
		}
		return static_cast<uint32_t>(defaultSlot);
	};

	// ---- メッシュ ----
	for (cgltf_size mi = 0; mi < d->meshes_count; ++mi)
	{
		const cgltf_mesh& gm = d->meshes[mi];
		bool hasUv0 = false, hasUv1 = false, hasColor = false, hasSkin = false;
		for (cgltf_size p = 0; p < gm.primitives_count; ++p)
			for (cgltf_size a = 0; a < gm.primitives[p].attributes_count; ++a)
			{
				const auto& at = gm.primitives[p].attributes[a];
				hasUv0   |= at.type == cgltf_attribute_type_texcoord && at.index == 0;
				hasUv1   |= at.type == cgltf_attribute_type_texcoord && at.index == 1;
				hasColor |= at.type == cgltf_attribute_type_color && at.index == 0;
				hasSkin  |= at.type == cgltf_attribute_type_joints && at.index == 0;
			}
		MeshBuilder mb(hasUv0, hasUv1, hasColor, hasSkin);
		ModelMesh mesh;
		mesh.name = gm.name ? gm.name : "Mesh" + std::to_string(mi);
		// モーフ: primitive をつなげた頂点の数で、あとで埋める
		struct PrimMorph { uint32_t first, count; const cgltf_primitive* prim; };
		std::vector<PrimMorph> prims;
		size_t targetCount = 0;
		for (cgltf_size p = 0; p < gm.primitives_count; ++p)
		{
			const cgltf_primitive& prim = gm.primitives[p];
			if (prim.type != cgltf_primitive_type_triangles)
			{
				diag.warn(mesh.name + ": 三角形でない primitive は飛ばします");
				continue;
			}
			const cgltf_accessor *pos = nullptr, *nor = nullptr, *uv0 = nullptr, *uv1 = nullptr, *col = nullptr, *jnt = nullptr, *wgt = nullptr;
			for (cgltf_size a = 0; a < prim.attributes_count; ++a)
			{
				const auto& at = prim.attributes[a];
				if (at.type == cgltf_attribute_type_position) pos = at.data;
				else if (at.type == cgltf_attribute_type_normal) nor = at.data;
				else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv0 = at.data;
				else if (at.type == cgltf_attribute_type_texcoord && at.index == 1) uv1 = at.data;
				else if (at.type == cgltf_attribute_type_color && at.index == 0) col = at.data;
				else if (at.type == cgltf_attribute_type_joints && at.index == 0) jnt = at.data;
				else if (at.type == cgltf_attribute_type_weights && at.index == 0) wgt = at.data;
			}
			if (!pos)
			{
				diag.warn(mesh.name + ": 位置のない primitive は飛ばします");
				continue;
			}
			const uint32_t first = static_cast<uint32_t>(mb.sources().size());
			const cgltf_size vcount = pos->count;
			for (cgltf_size v = 0; v < vcount; ++v)
			{
				Vertex vx;
				cgltf_accessor_read_float(pos, v, vx.pos, 3);
				if (nor) cgltf_accessor_read_float(nor, v, vx.normal, 3);
				if (uv0) cgltf_accessor_read_float(uv0, v, vx.uv0, 2);
				if (uv1) cgltf_accessor_read_float(uv1, v, vx.uv1, 2);
				if (col)
				{
					float c[4] = { 1, 1, 1, 1 };
					cgltf_accessor_read_float(col, v, c, col->type == cgltf_type_vec3 ? 3 : 4);
					std::memcpy(vx.color, c, sizeof(c));
				}
				if (jnt && wgt)
				{
					cgltf_uint j[4] = {};
					float w[4] = {};
					cgltf_accessor_read_uint(jnt, v, j, 4);
					cgltf_accessor_read_float(wgt, v, w, 4);
					std::vector<std::pair<uint32_t, float>> list;
					for (int k = 0; k < 4; ++k) list.emplace_back(j[k], w[k]);
					pickWeights(list, vx.joints, vx.weights);
				}
				mb.add(vx, static_cast<uint32_t>(first + v), false);   // glTF の頂点はもともと 1 つずつ
			}
			SubMesh sm;
			sm.indexOffset = mb.indexCount();
			if (prim.indices)
				for (cgltf_size i = 0; i < prim.indices->count; ++i)
					mb.index(first + static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i)));
			else
				for (cgltf_size i = 0; i < vcount; ++i)
					mb.index(first + static_cast<uint32_t>(i));
			sm.indexCount = mb.indexCount() - sm.indexOffset;
			sm.slot       = slotOf(prim.material);
			mesh.submeshes.push_back(sm);
			prims.push_back({ first, static_cast<uint32_t>(vcount), &prim });
			targetCount = std::max(targetCount, static_cast<size_t>(prim.targets_count));
			if (!nor)
				diag.warn(mesh.name + ": 法線がない primitive があります (0 のまま。モデルを書き出すときに法線を付けてください)");
		}
		mb.finish(mesh);

		// ---- モーフ ----
		for (size_t t = 0; t < targetCount; ++t)
		{
			MorphTarget mt;
			mt.name = t < gm.target_names_count && gm.target_names[t] ? gm.target_names[t] : "Morph" + std::to_string(t);
			mt.defaultWeight = t < gm.weights_count ? gm.weights[t] : 0.0f;
			mt.positions.assign(static_cast<size_t>(mesh.vertexCount) * 3, 0.0f);
			bool anyNormal = false;
			std::vector<float> normals(static_cast<size_t>(mesh.vertexCount) * 3, 0.0f);
			for (const auto& pm : prims)
			{
				if (t >= pm.prim->targets_count) continue;
				const cgltf_morph_target& tg = pm.prim->targets[t];
				for (cgltf_size a = 0; a < tg.attributes_count; ++a)
				{
					const auto& at = tg.attributes[a];
					std::vector<float>* dst = at.type == cgltf_attribute_type_position ? &mt.positions
					                        : at.type == cgltf_attribute_type_normal   ? &normals : nullptr;
					if (!dst) continue;
					anyNormal |= at.type == cgltf_attribute_type_normal;
					for (uint32_t v = 0; v < pm.count && v < at.data->count; ++v)
						cgltf_accessor_read_float(at.data, v, &(*dst)[(pm.first + v) * 3], 3);
				}
			}
			if (anyNormal) mt.normals = std::move(normals);
			mesh.morphs.push_back(std::move(mt));
		}
		computeBounds(mesh);
		model.meshes.push_back(std::move(mesh));
	}

	// ---- ノード ----
	for (cgltf_size i = 0; i < d->nodes_count; ++i)
	{
		const cgltf_node& gn = d->nodes[i];
		ModelNode n;
		n.name   = gn.name ? gn.name : "Node" + std::to_string(i);
		n.parent = gn.parent ? static_cast<int32_t>(gn.parent - d->nodes) : -1;
		if (gn.has_matrix)
			decompose(gn.matrix, n);
		else
		{
			if (gn.has_translation) n.translation = { gn.translation[0], gn.translation[1], gn.translation[2] };
			if (gn.has_rotation)    n.rotation    = { gn.rotation[0], gn.rotation[1], gn.rotation[2], gn.rotation[3] };
			if (gn.has_scale)       n.scale       = { gn.scale[0], gn.scale[1], gn.scale[2] };
		}
		n.mesh = gn.mesh ? static_cast<int32_t>(gn.mesh - d->meshes) : -1;
		n.skin = gn.skin ? static_cast<int32_t>(gn.skin - d->skins) : -1;
		model.nodes.push_back(n);
	}

	// ---- スキン ----
	for (cgltf_size i = 0; i < d->skins_count; ++i)
	{
		const cgltf_skin& gs = d->skins[i];
		ModelSkin s;
		s.name = gs.name ? gs.name : "Skin" + std::to_string(i);
		for (cgltf_size j = 0; j < gs.joints_count; ++j)
		{
			s.joints.push_back(static_cast<int32_t>(gs.joints[j] - d->nodes));
			float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
			if (gs.inverse_bind_matrices)
				cgltf_accessor_read_float(gs.inverse_bind_matrices, j, m, 16);
			s.inverseBind.insert(s.inverseBind.end(), m, m + 16);
		}
		model.skins.push_back(std::move(s));
	}

	for (auto& mesh : model.meshes)
		generateTangents(mesh);   // glTF の接線は使わず作り直す (向きを揃える)
	computeModelBounds(model);
	out = std::move(model);
	return true;
}

std::vector<std::string> gltfReferencedFiles(const fs::path& file, Diagnostics& diag)
{
	std::vector<std::string> out;
	Scope scope;
	cgltf_options options;
	std::memset(&options, 0, sizeof(options));
	options.file.read    = readCallback;
	options.file.release = releaseCallback;
	std::string bytes;
	if (!readFile(file, bytes) || cgltf_parse(&options, bytes.data(), bytes.size(), &scope.data) != cgltf_result_success)
		return diag.error("glTF を読めません"), out;
	auto add = [&](const char* uri) {
		if (!uri || std::strncmp(uri, "data:", 5) == 0) return;
		std::vector<char> buf(uri, uri + std::strlen(uri) + 1);
		cgltf_decode_uri(buf.data());
		out.emplace_back(buf.data());
	};
	for (cgltf_size i = 0; i < scope.data->buffers_count; ++i) add(scope.data->buffers[i].uri);
	for (cgltf_size i = 0; i < scope.data->images_count; ++i)  add(scope.data->images[i].uri);
	return out;
}
} // namespace sunset::asset::detail
