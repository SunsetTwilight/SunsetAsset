// ============================================================
//  src/model/ModelImporter.cpp
// ============================================================
#include "sunset/asset/model/ModelImporter.h"

#include "MeshBuilder.h"
#include "ModelFormats.h"

#include <algorithm>
#include <cctype>

namespace sunset::asset
{
namespace
{
bool isGltf(const std::filesystem::path& p)
{
	std::string ext = genericUtf8(p.extension());
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext == ".gltf" || ext == ".glb";
}

void applyScale(Model& m, float s)
{
	if (s == 1.0f)
		return;
	for (auto& mesh : m.meshes)
	{
		for (float& v : mesh.positions) v *= s;
		for (auto& mt : mesh.morphs)
			for (float& v : mt.positions) v *= s;
		detail::computeBounds(mesh);
	}
	for (auto& n : m.nodes)
		for (float& v : n.translation) v *= s;
	// 逆バインド行列 (骨の空間 ← メッシュの空間) は、平行移動の成分だけが単位を持つ
	for (auto& skin : m.skins)
		for (size_t j = 0; j + 15 < skin.inverseBind.size(); j += 16)
			for (size_t k = 12; k < 15; ++k) skin.inverseBind[j + k] *= s;
	detail::computeModelBounds(m);
}

class ModelImporter final : public Importer
{
public:
	std::string_view         name() const override { return "model"; }
	std::vector<std::string> extensions() const override { return { ".fbx", ".obj", ".gltf", ".glb" }; }
	uint32_t                 version() const override { return 1; }
	Json defaultSettings() const override { return { { "scale", 1.0 }, { "generateTangents", true } }; }

	bool import(const ImportContext& ctx, ArtifactWriter& out, Diagnostics& diag) const override
	{
		Model model;
		const bool ok = isGltf(ctx.source) ? detail::importGltf(ctx.source, model, diag) : detail::importUfbx(ctx.source, model, diag);
		if (!ok)
			return false;
		if (!ctx.settings.value("generateTangents", true))
			for (auto& mesh : model.meshes) mesh.tangents.clear();
		applyScale(model, ctx.settings.value("scale", 1.0f));
		if (model.meshes.empty())
			diag.warn("メッシュがありません");
		writeModel(model, out);
		return true;
	}

	std::vector<std::string> referencedFiles(const std::filesystem::path& source, Diagnostics& diag) const override
	{
		return isGltf(source) ? detail::gltfReferencedFiles(source, diag) : detail::ufbxReferencedFiles(source, diag);
	}
};
} // namespace

std::unique_ptr<Importer> makeModelImporter()
{
	return std::make_unique<ModelImporter>();
}

bool loadModel(const std::filesystem::path& artifact, Model& out, std::string* error)
{
	ArtifactReader reader;
	if (!reader.open(artifact, error))
		return false;
	if (reader.header().importer != "model")
	{
		if (error) *error = "モデルのキャッシュではありません (" + reader.header().importer + ")";
		return false;
	}
	return readModel(reader, out, error);
}
} // namespace sunset::asset
