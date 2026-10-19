// ============================================================
//  src/AssetDatabase.cpp
// ============================================================
#include "sunset/asset/AssetDatabase.h"

#include <algorithm>
#include <map>

namespace sunset::asset
{
namespace fs = std::filesystem;

namespace
{
int64_t fileTime(const fs::path& p)
{
	std::error_code ec;
	const auto t = fs::last_write_time(p, ec);
	return ec ? 0 : static_cast<int64_t>(t.time_since_epoch().count());
}
} // namespace

AssetDatabase::ScanResult AssetDatabase::scan(const fs::path& root, Diagnostics& diag)
{
	ScanResult result;
	m_root = fs::absolute(root).lexically_normal();
	m_records.clear();
	m_byId.clear();

	// ファイルと .meta を集める (パスの順に並べて、結果を毎回同じにする)
	std::map<std::string, fs::path> assets, metas;
	std::error_code ec;
	for (fs::recursive_directory_iterator it(m_root, ec), end; !ec && it != end; it.increment(ec))
	{
		if (!it->is_regular_file(ec))
			continue;
		const fs::path& p = it->path();
		const std::string rel = genericUtf8(p.lexically_relative(m_root));
		if (p.extension() == ".tmp")
			continue;
		if (isMetaFile(p))
			metas.emplace(rel, p);
		else
			assets.emplace(rel, p);
	}
	if (ec)
		diag.error("アセットのフォルダを読めません: " + pathToUtf8(m_root) + " (" + ec.message() + ")");

	// 取り込める拡張子のファイルで .meta のないものに、.meta を作る
	for (const auto& [rel, p] : assets)
	{
		const Importer* imp = m_registry.forFile(p);
		const fs::path  mp  = metaPathFor(p);
		if (!imp || metas.count(rel + ".meta"))
			continue;
		AssetMeta m;
		m.id       = Guid::generate();
		m.importer = std::string(imp->name());
		std::string error;
		if (writeMeta(mp, m, &error))
		{
			metas.emplace(rel + ".meta", mp);
			++result.created;
		}
		else
			diag.error(rel + ": .meta を作れません: " + error);
	}

	// .meta を読んで表を作る
	for (const auto& [metaRel, mp] : metas)
	{
		AssetRecord r;
		r.metaFile = mp;
		if (!readMeta(mp, r.meta, diag))
			continue;
		r.path = metaRel.substr(0, metaRel.size() - 5);   // ".meta" を除く
		r.source = r.meta.external() ? pathFromUtf8(r.meta.source) : assetPathForMeta(mp);
		r.missing = !fs::is_regular_file(r.source, ec);
		r.importer = m_registry.byName(r.meta.importer);
		if (!r.importer)
			diag.warn(r.path + ": 知らない取り込み (" + r.meta.importer + ")");
		if (r.missing)
			diag.warn(r.path + (r.meta.external() ? ": 外部のファイルが見つかりません: " + r.meta.source : ": ファイルがありません (.meta だけが残っています)"));

		// 同じ ID: 先に見つかった (パスの順で前の) ものを残し、後のものに新しい ID を付ける
		if (m_byId.count(r.meta.id))
		{
			const Guid old = r.meta.id;
			r.meta.id = Guid::generate();
			std::string error;
			if (writeMeta(mp, r.meta, &error))
			{
				++result.reIded;
				diag.warn(r.path + ": ID " + old.toString() + " が " + m_records[m_byId[old]].path + " と重なっていたので、新しい ID を付けました");
			}
			else
			{
				diag.error(r.path + ": ID が重なっていますが、.meta を書き直せません: " + error);
				continue;
			}
		}
		r.id = r.meta.id;
		m_byId.emplace(r.id, m_records.size());
		m_records.push_back(std::move(r));
	}
	return result;
}

const AssetRecord* AssetDatabase::find(const Guid& id) const
{
	const auto it = m_byId.find(id);
	return it == m_byId.end() ? nullptr : &m_records[it->second];
}

const AssetRecord* AssetDatabase::findByPath(const std::string& path) const
{
	for (const auto& r : m_records)
		if (r.path == path)
			return &r;
	return nullptr;
}

fs::path AssetDatabase::artifactPath(const fs::path& cacheDir, const Guid& id)
{
	return cacheDir / (id.toString() + ".bin");
}

ArtifactHeader AssetDatabase::currentHeader(const AssetRecord& r, const Importer& imp) const
{
	ArtifactHeader h;
	h.importer        = std::string(imp.name());
	h.importerVersion = imp.version();
	std::error_code ec;
	h.sourceSize   = static_cast<uint64_t>(fs::file_size(r.source, ec));
	h.sourceTime   = fileTime(r.source);
	h.settingsHash = hash64(ImporterRegistry::mergeSettings(imp.defaultSettings(), r.meta.settings).dump());
	return h;
}

bool AssetDatabase::upToDate(const AssetRecord& r, const fs::path& cacheDir) const
{
	if (!r.importer || r.missing)
		return false;
	ArtifactHeader cached;
	if (!ArtifactReader::readHeader(artifactPath(cacheDir, r.id), cached))
		return false;
	return cached.sameInputs(currentHeader(r, *r.importer));
}

bool AssetDatabase::import(const AssetRecord& r, const fs::path& cacheDir, fs::path& artifact, Diagnostics& diag) const
{
	artifact = artifactPath(cacheDir, r.id);
	if (!r.importer)
		return diag.error(r.path + ": 知らない取り込み (" + r.meta.importer + ")"), false;
	if (r.missing)
		return diag.error(r.path + ": 元のファイルがありません"), false;
	if (upToDate(r, cacheDir))
		return true;

	ArtifactHeader header = currentHeader(r, *r.importer);
	std::string bytes;
	if (!readFile(r.source, bytes))
		return diag.error(r.path + ": 読めません: " + pathToUtf8(r.source)), false;
	header.sourceHash = hash64(bytes);
	bytes.clear();

	ImportContext ctx;
	ctx.id       = r.id;
	ctx.source   = r.source;
	ctx.name     = r.path;
	ctx.settings = ImporterRegistry::mergeSettings(r.importer->defaultSettings(), r.meta.settings);
	ArtifactWriter writer;
	Diagnostics local;
	const bool ok = r.importer->import(ctx, writer, local);
	for (auto& w : local.warnings) diag.warn(r.path + ": " + w);
	for (auto& e : local.errors)   diag.error(r.path + ": " + e);
	if (!ok)
		return false;
	std::string error;
	if (!writer.write(artifact, header, &error))
		return diag.error(r.path + ": キャッシュを書けません: " + error), false;
	return true;
}

bool AssetDatabase::linkExternal(const fs::path& external, const std::string& metaName, Guid& id, std::string& error) const
{
	if (m_root.empty())
		return error = "先に scan してください", false;
	std::error_code ec;
	if (!fs::is_regular_file(external, ec))
		return error = "ファイルがありません: " + pathToUtf8(external), false;
	const Importer* imp = m_registry.forFile(external);
	if (!imp)
		return error = "取り込めない種類のファイルです: " + pathToUtf8(external.filename()), false;
	const fs::path mp = metaPathFor(m_root / pathFromUtf8(metaName));
	if (fs::exists(mp, ec) || fs::exists(assetPathForMeta(mp), ec))
		return error = "同じ名前があります: " + metaName, false;
	AssetMeta m;
	m.id       = Guid::generate();
	m.importer = std::string(imp->name());
	m.source   = genericUtf8(fs::absolute(external).lexically_normal());
	if (!writeMeta(mp, m, &error))
		return false;
	id = m.id;
	return true;
}
} // namespace sunset::asset
