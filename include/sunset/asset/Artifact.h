#pragma once
// ============================================================
//  sunset/asset/Artifact.h
//
//  キャッシュの入れ物 (Cache/assets/<id>.bin): 取り込んだ結果を、種類によらない形で入れる。
//
//    ヘッダー: 目印 "SAST"、入れ物の版、取り込みの名前と版、元のファイルの大きさ / 更新時刻 / ハッシュ、設定のハッシュ
//    チャンク: (4 文字のタグ、大きさ、中身) の並び。中身は 8 バイトに揃える
//
//  ヘッダーが今と違えば (取り込みの版、元のファイル、設定のどれかが変わった) 作り直す。
//  数値はリトルエンディアン (Windows / x64 の並びのまま)。
// ============================================================

#include "Common.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace sunset::asset
{
constexpr uint32_t k_ArtifactVersion = 1;

using ChunkTag = std::array<char, 4>;
constexpr ChunkTag chunkTag(const char (&s)[5]) { return { s[0], s[1], s[2], s[3] }; }

struct ArtifactHeader
{
	uint32_t    containerVersion = k_ArtifactVersion;
	std::string importer;
	uint32_t    importerVersion = 0;
	uint64_t    sourceSize = 0;
	int64_t     sourceTime = 0;   // 更新時刻 (file_time_type の数)
	uint64_t    sourceHash = 0;   // 元のファイルの中身 (取り込んだときのもの)
	uint64_t    settingsHash = 0;

	// キャッシュを使ってよいか (ハッシュは比べない。大きさ / 時刻 / 版 / 設定で判断する)
	bool sameInputs(const ArtifactHeader& o) const
	{
		return containerVersion == o.containerVersion && importer == o.importer && importerVersion == o.importerVersion
		    && sourceSize == o.sourceSize && sourceTime == o.sourceTime && settingsHash == o.settingsHash;
	}
};

class ArtifactWriter
{
public:
	void add(ChunkTag tag, std::string data) { m_chunks.push_back({ tag, std::move(data) }); }
	void add(ChunkTag tag, const void* data, size_t size) { add(tag, std::string(static_cast<const char*>(data), size)); }
	// 型の配列をそのまま入れる (POD のみ)
	template<typename T>
	void addArray(ChunkTag tag, std::span<const T> items)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		add(tag, items.data(), items.size_bytes());
	}

	// ファイルの中身 (ヘッダー + チャンク) を作る
	std::string serialize(const ArtifactHeader& header) const;
	bool        write(const std::filesystem::path& path, const ArtifactHeader& header, std::string* error = nullptr) const;

private:
	struct Chunk
	{
		ChunkTag    tag;
		std::string data;
	};
	std::vector<Chunk> m_chunks;
};

class ArtifactReader
{
public:
	// ファイル全体を読む。形が違えば false (error に理由)
	bool open(const std::filesystem::path& path, std::string* error = nullptr);
	bool parse(std::string bytes, std::string* error = nullptr);
	// ヘッダーだけを読む (キャッシュの照合用。チャンクは読まない)
	static bool readHeader(const std::filesystem::path& path, ArtifactHeader& out);

	const ArtifactHeader& header() const { return m_header; }
	// 最初に見つかったタグのチャンク (なければ空)
	std::span<const char> find(ChunkTag tag) const;
	// チャンクを型の配列として読む (大きさが型で割り切れなければ空)
	template<typename T>
	std::vector<T> array(ChunkTag tag) const
	{
		static_assert(std::is_trivially_copyable_v<T>);
		const auto c = find(tag);
		if (c.size() % sizeof(T) != 0)
			return {};
		std::vector<T> out(c.size() / sizeof(T));
		if (!c.empty())
			std::memcpy(out.data(), c.data(), c.size());
		return out;
	}
	size_t chunkCount() const { return m_chunks.size(); }

private:
	struct Chunk
	{
		ChunkTag tag;
		size_t   offset, size;
	};
	std::string        m_bytes;
	ArtifactHeader     m_header;
	std::vector<Chunk> m_chunks;
};
} // namespace sunset::asset
