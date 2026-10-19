#pragma once
// ============================================================
//  sunset/asset/AssetDatabase.h
//
//  アセットのフォルダ (プロジェクトの assets/) を走査し、ID → ファイルの表を作る。取り込んだ結果をキャッシュする。
//
//    scan          .meta を読む。取り込みが登録されている拡張子で .meta のないファイルには .meta を作る (新しい ID)。
//                  同じ ID の .meta が 2 つあれば (ファイルを .meta ごと複製したなど)、パスの順で後のほうに新しい ID を付けて書き直す。
//                  外部の参照 (.meta の source) は、その元のファイルを指す。元のファイルがなければ missing
//    import        キャッシュ (cacheDir/<id>.bin) が古ければ取り込み直す。新しければそのまま
//    linkExternal  外部のファイルを参照する .meta を作る (コピーしない)
//
//  変更の監視はしない (使う側が scan / import を呼び直す)。1 つのスレッドから使うこと
//  (取り込み自体は Importer がスレッドに依存しないので、別のスレッドで import を呼んでもよい)。
// ============================================================

#include "Artifact.h"
#include "Common.h"
#include "Guid.h"
#include "Importer.h"
#include "Meta.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace sunset::asset
{
struct AssetRecord
{
	Guid                  id;
	std::string           path;        // アセットのフォルダからの相対パス (区切りは "/")。外部の参照では .meta の名前から .meta を除いたもの
	std::filesystem::path source;      // 元のファイル (絶対パス)
	std::filesystem::path metaFile;    // .meta (絶対パス)
	AssetMeta             meta;
	const Importer*       importer = nullptr;   // 登録されていない取り込みなら nullptr
	bool                  missing  = false;     // 元のファイルがない (外部の参照が切れた / .meta だけが残った)
};

class AssetDatabase
{
public:
	explicit AssetDatabase(const ImporterRegistry& registry) : m_registry(registry) {}

	// root を走査して表を作り直す。作った / 書き直した .meta の数を返す
	struct ScanResult
	{
		size_t created  = 0;   // 新しく作った .meta
		size_t reIded   = 0;   // ID が重なったので付け直した .meta
	};
	ScanResult scan(const std::filesystem::path& root, Diagnostics& diag);

	const std::filesystem::path&    root() const { return m_root; }
	const std::vector<AssetRecord>& records() const { return m_records; }
	const AssetRecord*              find(const Guid& id) const;
	const AssetRecord*              findByPath(const std::string& path) const;

	// キャッシュが今の元のファイル / 設定 / 取り込みの版と合うか
	bool upToDate(const AssetRecord& record, const std::filesystem::path& cacheDir) const;
	// 古ければ取り込んで cacheDir/<id>.bin に書く。artifact にそのパス。失敗したら false (diag に理由)
	bool import(const AssetRecord& record, const std::filesystem::path& cacheDir, std::filesystem::path& artifact,
	            Diagnostics& diag) const;
	static std::filesystem::path artifactPath(const std::filesystem::path& cacheDir, const Guid& id);

	// 外部のファイル (external) を参照する .meta を root/<metaName>.meta に作る (scan し直すと表に入る)
	bool linkExternal(const std::filesystem::path& external, const std::string& metaName, Guid& id, std::string& error) const;

private:
	ArtifactHeader currentHeader(const AssetRecord& record, const Importer& importer) const;

	const ImporterRegistry&                 m_registry;
	std::filesystem::path                   m_root;
	std::vector<AssetRecord>                m_records;
	std::unordered_map<Guid, size_t>        m_byId;
};
} // namespace sunset::asset
