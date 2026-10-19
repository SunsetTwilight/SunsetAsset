// ============================================================
//  src/Common.cpp
// ============================================================
#include "sunset/asset/Common.h"

#include <fstream>
#include <sstream>

namespace sunset::asset
{
namespace fs = std::filesystem;

fs::path pathFromUtf8(const std::string& s)
{
	return fs::path(std::u8string(s.begin(), s.end()));
}

std::string pathToUtf8(const fs::path& p)
{
	const std::u8string u = p.u8string();
	return std::string(u.begin(), u.end());
}

std::string genericUtf8(const fs::path& p)
{
	const std::u8string u = p.generic_u8string();
	return std::string(u.begin(), u.end());
}

bool readFile(const fs::path& path, std::string& out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

bool writeFileAtomic(const fs::path& path, const std::string& data, std::string* error)
{
	std::error_code ec;
	if (path.has_parent_path())
		fs::create_directories(path.parent_path(), ec);
	fs::path tmp = path;
	tmp += ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f || !f.write(data.data(), static_cast<std::streamsize>(data.size())))
		{
			if (error) *error = "書き込めません: " + pathToUtf8(tmp);
			return false;
		}
	}
	fs::rename(tmp, path, ec);
	if (ec)
	{
		fs::remove(tmp, ec);
		if (error) *error = "置き換えに失敗しました: " + pathToUtf8(path);
		return false;
	}
	return true;
}

uint64_t hash64(const void* data, size_t size, uint64_t seed)
{
	const auto* p = static_cast<const unsigned char*>(data);
	uint64_t h = seed;
	for (size_t i = 0; i < size; ++i)
	{
		h ^= p[i];
		h *= 0x100000001b3ull;
	}
	return h;
}
} // namespace sunset::asset
