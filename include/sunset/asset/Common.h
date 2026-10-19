#pragma once
// ============================================================
//  sunset/asset/Common.h
//
//  ライブラリで共通の型: JSON、診断 (エラー / 警告)、UTF-8 とパスの変換。
// ============================================================

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace sunset::asset
{
using Json = nlohmann::json;

// 取り込みや走査で見つけた問題。止めずに集めて、使う側がログなどに出す
struct Diagnostics
{
	std::vector<std::string> errors;
	std::vector<std::string> warnings;

	void error(std::string text) { errors.push_back(std::move(text)); }
	void warn(std::string text)  { warnings.push_back(std::move(text)); }
	bool ok() const { return errors.empty(); }
};

// UTF-8 の文字列 ⇔ パス (Windows でも日本語のパスを正しく扱う)
std::filesystem::path pathFromUtf8(const std::string& s);
std::string           pathToUtf8(const std::filesystem::path& p);          // 区切りはそのまま
std::string           genericUtf8(const std::filesystem::path& p);         // 区切りは "/"

// ファイル全体の読み書き。書くときは一時ファイルに書いてから置き換える
bool readFile(const std::filesystem::path& path, std::string& out);
bool writeFileAtomic(const std::filesystem::path& path, const std::string& data, std::string* error = nullptr);

// 64 ビットの FNV-1a (キャッシュの照合用。暗号用ではない)
uint64_t hash64(const void* data, size_t size, uint64_t seed = 0xcbf29ce484222325ull);
inline uint64_t hash64(const std::string& s) { return hash64(s.data(), s.size()); }
} // namespace sunset::asset
