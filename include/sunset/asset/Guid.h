#pragma once
// ============================================================
//  sunset/asset/Guid.h
//
//  アセットの ID (128 ビット)。.meta に書き、シーンなどはこの ID でアセットを参照する
//  (ファイルを移しても、名前を変えても参照が切れない)。
//  文字列は小文字の 16 進 32 文字 ("3f2a…")。ファイル名や JSON のキーにそのまま使える。
//  作るときは乱数 (RFC 4122 の version 4 と同じビットの並び)。
// ============================================================

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sunset::asset
{
struct Guid
{
	std::array<uint8_t, 16> bytes {};

	// 乱数で新しく作る
	static Guid generate();
	// 小文字 / 大文字の 16 進 32 文字。ハイフン付き (8-4-4-4-12) も受け付ける。形が違えば nullopt
	static std::optional<Guid> parse(std::string_view text);

	std::string toString() const;   // 小文字の 16 進 32 文字
	bool        isNull() const;     // すべて 0

	auto operator<=>(const Guid&) const = default;
};
} // namespace sunset::asset

template<>
struct std::hash<sunset::asset::Guid>
{
	size_t operator()(const sunset::asset::Guid& g) const noexcept
	{
		uint64_t a = 0, b = 0;
		for (int i = 0; i < 8; ++i)
		{
			a = (a << 8) | g.bytes[static_cast<size_t>(i)];
			b = (b << 8) | g.bytes[static_cast<size_t>(i + 8)];
		}
		return static_cast<size_t>(a ^ (b * 0x9E3779B97F4A7C15ull));
	}
};
