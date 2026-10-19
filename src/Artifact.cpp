// ============================================================
//  src/Artifact.cpp
// ============================================================
#include "sunset/asset/Artifact.h"

#include <fstream>

namespace sunset::asset
{
namespace
{
constexpr char k_Magic[4] = { 'S', 'A', 'S', 'T' };

template<typename T>
void put(std::string& out, T v)
{
	static_assert(std::is_trivially_copyable_v<T>);
	out.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

void putString(std::string& out, const std::string& s)
{
	put<uint32_t>(out, static_cast<uint32_t>(s.size()));
	out += s;
}

void align8(std::string& out)
{
	while (out.size() % 8 != 0)
		out.push_back('\0');
}

// 読み取り位置を進めながら読む (足りなければ false)
struct Cursor
{
	const std::string& bytes;
	size_t             pos = 0;

	template<typename T>
	bool get(T& v)
	{
		if (bytes.size() - pos < sizeof(T)) return false;
		std::memcpy(&v, bytes.data() + pos, sizeof(T));
		pos += sizeof(T);
		return true;
	}
	bool getString(std::string& s)
	{
		uint32_t n = 0;
		if (!get(n) || bytes.size() - pos < n) return false;
		s.assign(bytes.data() + pos, n);
		pos += n;
		return true;
	}
	void align8() { pos = (pos + 7) & ~size_t(7); }
};

std::string serializeHeader(const ArtifactHeader& h)
{
	std::string out(k_Magic, 4);
	put<uint32_t>(out, h.containerVersion);
	putString(out, h.importer);
	put<uint32_t>(out, h.importerVersion);
	put<uint64_t>(out, h.sourceSize);
	put<int64_t>(out, h.sourceTime);
	put<uint64_t>(out, h.sourceHash);
	put<uint64_t>(out, h.settingsHash);
	return out;
}

bool parseHeader(Cursor& c, ArtifactHeader& h)
{
	char magic[4] = {};
	for (char& m : magic)
		if (!c.get(m)) return false;
	if (std::memcmp(magic, k_Magic, 4) != 0) return false;
	return c.get(h.containerVersion) && h.containerVersion == k_ArtifactVersion && c.getString(h.importer) && c.get(h.importerVersion)
	    && c.get(h.sourceSize) && c.get(h.sourceTime) && c.get(h.sourceHash) && c.get(h.settingsHash);
}
} // namespace

std::string ArtifactWriter::serialize(const ArtifactHeader& header) const
{
	std::string out = serializeHeader(header);
	put<uint32_t>(out, static_cast<uint32_t>(m_chunks.size()));
	for (const Chunk& c : m_chunks)
	{
		align8(out);
		out.append(c.tag.data(), 4);
		put<uint32_t>(out, 0);   // 予約 (8 バイトに揃える)
		put<uint64_t>(out, static_cast<uint64_t>(c.data.size()));
		out += c.data;
	}
	return out;
}

bool ArtifactWriter::write(const std::filesystem::path& path, const ArtifactHeader& header, std::string* error) const
{
	return writeFileAtomic(path, serialize(header), error);
}

bool ArtifactReader::open(const std::filesystem::path& path, std::string* error)
{
	std::string bytes;
	if (!readFile(path, bytes))
	{
		if (error) *error = "読めません: " + pathToUtf8(path);
		return false;
	}
	return parse(std::move(bytes), error);
}

bool ArtifactReader::parse(std::string bytes, std::string* error)
{
	m_bytes = std::move(bytes);
	m_chunks.clear();
	m_header = {};
	Cursor c { m_bytes };
	uint32_t count = 0;
	if (!parseHeader(c, m_header) || !c.get(count))
	{
		if (error) *error = "キャッシュの形が違います (目印か版)";
		return false;
	}
	for (uint32_t i = 0; i < count; ++i)
	{
		c.align8();
		Chunk ch {};
		uint32_t reserved = 0;
		uint64_t size = 0;
		bool tagOk = true;
		for (char& t : ch.tag)
			tagOk = tagOk && c.get(t);
		if (!tagOk || !c.get(reserved) || !c.get(size) || m_bytes.size() - c.pos < size)
		{
			if (error) *error = "チャンクが途中で切れています";
			return false;
		}
		ch.offset = c.pos;
		ch.size   = static_cast<size_t>(size);
		c.pos += ch.size;
		m_chunks.push_back(ch);
	}
	return true;
}

bool ArtifactReader::readHeader(const std::filesystem::path& path, ArtifactHeader& out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return false;
	std::string bytes(4096, '\0');   // ヘッダーは取り込みの名前の分だけで、4 KiB に収まる
	f.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	bytes.resize(static_cast<size_t>(f.gcount()));
	Cursor c { bytes };
	return parseHeader(c, out);
}

std::span<const char> ArtifactReader::find(ChunkTag tag) const
{
	for (const Chunk& c : m_chunks)
		if (c.tag == tag)
			return { m_bytes.data() + c.offset, c.size };
	return {};
}
} // namespace sunset::asset
