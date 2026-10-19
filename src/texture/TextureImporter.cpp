// ============================================================
//  src/texture/TextureImporter.cpp
// ============================================================
#include "sunset/asset/texture/Texture.h"

#include <stb_image.h>

#include <cstring>

namespace sunset::asset
{
namespace
{
constexpr ChunkTag k_Info   = chunkTag("TXIN");
constexpr ChunkTag k_Pixels = chunkTag("TXPX");

class TextureImporter final : public Importer
{
public:
	std::string_view         name() const override { return "texture"; }
	std::vector<std::string> extensions() const override { return { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }; }
	uint32_t                 version() const override { return 1; }
	Json                     defaultSettings() const override { return { { "srgb", true } }; }

	bool import(const ImportContext& ctx, ArtifactWriter& out, Diagnostics& diag) const override
	{
		std::string bytes;
		if (!readFile(ctx.source, bytes))
			return diag.error("読めません"), false;
		Texture tex;
		std::string error;
		if (!decodeImage(bytes.data(), bytes.size(), tex, &error))
			return diag.error(error), false;
		tex.srgb = ctx.settings.value("srgb", true);
		writeTexture(tex, out);
		return true;
	}
};
} // namespace

std::unique_ptr<Importer> makeTextureImporter()
{
	return std::make_unique<TextureImporter>();
}

bool decodeImage(const void* data, size_t size, Texture& out, std::string* error)
{
	int w = 0, h = 0, comp = 0;
	stbi_uc* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size), &w, &h, &comp, 4);
	if (!pixels)
	{
		if (error) *error = std::string("画像を読めません: ") + (stbi_failure_reason() ? stbi_failure_reason() : "不明");
		return false;
	}
	out.width    = static_cast<uint32_t>(w);
	out.height   = static_cast<uint32_t>(h);
	out.channels = static_cast<uint32_t>(comp);
	out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
	stbi_image_free(pixels);
	return true;
}

void writeTexture(const Texture& t, ArtifactWriter& out)
{
	out.add(k_Info, Json { { "width", t.width }, { "height", t.height }, { "channels", t.channels }, { "srgb", t.srgb } }.dump());
	out.add(k_Pixels, t.rgba.data(), t.rgba.size());
}

bool readTexture(const ArtifactReader& in, Texture& out, std::string* error)
{
	const auto info = in.find(k_Info);
	try
	{
		const Json j = Json::parse(std::string_view(info.data(), info.size()));
		Texture t;
		t.width    = j.at("width").get<uint32_t>();
		t.height   = j.at("height").get<uint32_t>();
		t.channels = j.value("channels", 4u);
		t.srgb     = j.value("srgb", true);
		const auto px = in.find(k_Pixels);
		if (px.size() != static_cast<size_t>(t.width) * t.height * 4)
		{
			if (error) *error = "テクスチャの画素の大きさが合いません";
			return false;
		}
		t.rgba.assign(reinterpret_cast<const uint8_t*>(px.data()), reinterpret_cast<const uint8_t*>(px.data()) + px.size());
		out = std::move(t);
		return true;
	}
	catch (const std::exception& e)
	{
		if (error) *error = std::string("テクスチャの情報を読めません: ") + e.what();
		return false;
	}
}

bool loadTexture(const std::filesystem::path& artifact, Texture& out, std::string* error)
{
	ArtifactReader reader;
	if (!reader.open(artifact, error))
		return false;
	if (reader.header().importer != "texture")
	{
		if (error) *error = "テクスチャのキャッシュではありません (" + reader.header().importer + ")";
		return false;
	}
	return readTexture(reader, out, error);
}
} // namespace sunset::asset
