// ============================================================
//  src/Guid.cpp
// ============================================================
#include "sunset/asset/Guid.h"

#include <mutex>
#include <random>

namespace sunset::asset
{
Guid Guid::generate()
{
	// random_device はプラットフォームの乱数源 (Windows では BCryptGenRandom)。呼ぶたびに読むと遅いので、
	// 一度だけ種にして 64 ビットの生成器を回す (重ならないことだけが要る)
	static std::mutex       mutex;
	static std::mt19937_64  rng = [] {
		std::random_device rd;
		std::seed_seq seq { rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd() };
		return std::mt19937_64(seq);
	}();
	Guid g;
	{
		std::lock_guard lock(mutex);
		for (int i = 0; i < 2; ++i)
		{
			const uint64_t v = rng();
			for (int k = 0; k < 8; ++k)
				g.bytes[static_cast<size_t>(i * 8 + k)] = static_cast<uint8_t>(v >> (k * 8));
		}
	}
	g.bytes[6] = static_cast<uint8_t>((g.bytes[6] & 0x0F) | 0x40);   // version 4
	g.bytes[8] = static_cast<uint8_t>((g.bytes[8] & 0x3F) | 0x80);   // variant (RFC 4122)
	return g;
}

std::optional<Guid> Guid::parse(std::string_view text)
{
	auto hex = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	std::string digits;
	if (text.size() == 36)
	{
		// 8-4-4-4-12 のハイフンの位置だけを許す
		for (size_t i = 0; i < text.size(); ++i)
		{
			const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
			if (dash != (text[i] == '-'))
				return std::nullopt;
			if (!dash)
				digits.push_back(text[i]);
		}
	}
	else if (text.size() == 32)
		digits.assign(text);
	else
		return std::nullopt;

	Guid g;
	for (size_t i = 0; i < 16; ++i)
	{
		const int hi = hex(digits[i * 2]), lo = hex(digits[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			return std::nullopt;
		g.bytes[i] = static_cast<uint8_t>(hi * 16 + lo);
	}
	return g;
}

std::string Guid::toString() const
{
	static const char* k = "0123456789abcdef";
	std::string s;
	s.reserve(32);
	for (uint8_t b : bytes)
	{
		s.push_back(k[b >> 4]);
		s.push_back(k[b & 15]);
	}
	return s;
}

bool Guid::isNull() const
{
	for (uint8_t b : bytes)
		if (b != 0)
			return false;
	return true;
}
} // namespace sunset::asset
