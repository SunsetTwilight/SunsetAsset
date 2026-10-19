// ============================================================
//  src/Meta.cpp
// ============================================================
#include "sunset/asset/Meta.h"

namespace sunset::asset
{
namespace fs = std::filesystem;

fs::path metaPathFor(const fs::path& asset)
{
	fs::path p = asset;
	p += ".meta";
	return p;
}

bool isMetaFile(const fs::path& p)
{
	return p.extension() == ".meta";
}

fs::path assetPathForMeta(const fs::path& meta)
{
	fs::path p = meta;
	if (isMetaFile(p))
		p.replace_extension();
	return p;
}

Json metaToJson(const AssetMeta& m)
{
	Json j = { { "version", m.version }, { "id", m.id.toString() }, { "importer", m.importer },
	           { "settings", m.settings.is_object() ? m.settings : Json::object() } };
	if (m.external())
		j["source"] = m.source;
	return j;
}

bool metaFromJson(const Json& json, AssetMeta& out, Diagnostics& diag)
{
	const size_t before = diag.errors.size();
	if (!json.is_object())
		return diag.error("(root): オブジェクトが必要です"), false;
	AssetMeta m;
	bool hasId = false;
	for (const auto& [key, v] : json.items())
	{
		if (key == "version")
		{
			if (!v.is_number_integer()) diag.error("version: 整数が必要です");
			else if ((m.version = v.get<int>()) > k_MetaVersion)
				diag.warn("version: 新しい形式の .meta です。知らない項目は無視します");
		}
		else if (key == "id")
		{
			const auto g = v.is_string() ? Guid::parse(v.get<std::string>()) : std::nullopt;
			if (!g || g->isNull()) diag.error("id: 16 進 32 文字の ID が必要です");
			else { m.id = *g; hasId = true; }
		}
		else if (key == "importer")
		{
			if (v.is_string() && !v.get<std::string>().empty()) m.importer = v.get<std::string>();
			else diag.error("importer: 取り込みの名前が必要です");
		}
		else if (key == "settings")
		{
			if (v.is_object()) m.settings = v;
			else diag.error("settings: オブジェクトが必要です");
		}
		else if (key == "source")
		{
			if (v.is_string()) m.source = v.get<std::string>();
			else if (!v.is_null()) diag.error("source: 文字列が必要です");
		}
		else
			diag.warn(key + ": 知らないキーなので無視します");
	}
	if (!hasId && diag.errors.size() == before)
		diag.error("id: ありません");
	if (m.importer.empty() && diag.errors.size() == before)
		diag.error("importer: ありません");
	if (diag.errors.size() != before)
		return false;
	out = std::move(m);
	return true;
}

bool readMeta(const fs::path& metaFile, AssetMeta& out, Diagnostics& diag)
{
	std::string text;
	if (!readFile(metaFile, text))
		return diag.error("読めません: " + pathToUtf8(metaFile)), false;
	Json json;
	try
	{
		json = Json::parse(text);
	}
	catch (const std::exception& e)
	{
		diag.error(pathToUtf8(metaFile) + ": JSON: " + e.what());
		return false;
	}
	Diagnostics local;
	const bool ok = metaFromJson(json, out, local);
	for (auto& e : local.errors)   diag.error(pathToUtf8(metaFile) + ": " + e);
	for (auto& w : local.warnings) diag.warn(pathToUtf8(metaFile) + ": " + w);
	return ok;
}

bool writeMeta(const fs::path& metaFile, const AssetMeta& meta, std::string* error)
{
	return writeFileAtomic(metaFile, metaToJson(meta).dump(2) + "\n", error);
}
} // namespace sunset::asset
