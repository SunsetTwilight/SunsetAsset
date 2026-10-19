#pragma once
// ============================================================
//  sunset/asset/Importer.h
//
//  取り込み (importer) の登録。アセットの種類 (モデル、テクスチャ、音、独自のデータ …) ごとに 1 つ。
//  元のファイルを読み、種類ごとの形にして ArtifactWriter に書く。状態を持たず、どのスレッドから呼んでもよい。
//  使う側のアプリも、独自のデータの取り込みを登録できる。
// ============================================================

#include "Artifact.h"
#include "Common.h"
#include "Guid.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sunset::asset
{
struct ImportContext
{
	Guid                  id;
	std::filesystem::path source;     // 元のファイル (絶対パス)
	std::string           name;       // 表示用の名前 (assets/ からの相対パスなど)
	Json                  settings;   // 既定値に .meta の settings を重ねたもの
};

class Importer
{
public:
	virtual ~Importer() = default;

	virtual std::string_view         name() const = 0;          // "model"
	virtual std::vector<std::string> extensions() const = 0;    // 小文字、"." 付き (".fbx")。".tar.gz" のような 2 段も可
	virtual uint32_t                 version() const = 0;       // 変換の仕方を変えたら上げる (キャッシュを作り直す)
	virtual Json                     defaultSettings() const { return Json::object(); }

	// 取り込む。失敗したら false (diag.errors に理由)。警告は diag.warnings
	virtual bool import(const ImportContext& ctx, ArtifactWriter& out, Diagnostics& diag) const = 0;

	// 元のファイルが参照しているファイル (モデルのテクスチャなど)。元のファイルからの相対パス (区切りは "/")。
	// プロジェクトに取り込むとき、一緒にコピーするために使う
	virtual std::vector<std::string> referencedFiles(const std::filesystem::path& /*source*/, Diagnostics& /*diag*/) const { return {}; }
};

class ImporterRegistry
{
public:
	// 同じ名前、同じ拡張子があれば false (登録しない)
	bool add(std::unique_ptr<Importer> importer);

	const Importer* byName(std::string_view name) const;
	// ファイル名の末尾で探す (大文字と小文字は区別しない。長い拡張子を優先)
	const Importer* forFile(const std::filesystem::path& file) const;
	std::vector<const Importer*> all() const;

	// 既定値に settings を重ねる (オブジェクトどうしは中まで重ねる)
	static Json mergeSettings(const Json& defaults, const Json& settings);

private:
	std::vector<std::unique_ptr<Importer>> m_importers;
};
} // namespace sunset::asset
