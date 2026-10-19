// ============================================================
//  GUID の単体テスト
// ============================================================
#include <doctest/doctest.h>
#include "sunset/asset/Guid.h"
#include "sunset/asset/Version.h"

#include <unordered_set>

using namespace sunset::asset;

TEST_CASE("GUID: 作る、文字列にする、読む")
{
	const Guid a = Guid::generate();
	CHECK_FALSE(a.isNull());
	CHECK((a.bytes[6] >> 4) == 4);              // version 4
	CHECK((a.bytes[8] & 0xC0) == 0x80);         // variant
	const std::string s = a.toString();
	CHECK(s.size() == 32);
	CHECK(s.find_first_not_of("0123456789abcdef") == std::string::npos);
	CHECK(Guid::parse(s) == a);

	// 大文字とハイフン付きも読む
	const auto b = Guid::parse("3F2A1B4C-5D6E-4F70-8192-A3B4C5D6E7F8");
	REQUIRE(b);
	CHECK(b->toString() == "3f2a1b4c5d6e4f708192a3b4c5d6e7f8");

	// 形の違うもの
	CHECK_FALSE(Guid::parse(""));
	CHECK_FALSE(Guid::parse("3f2a"));
	CHECK_FALSE(Guid::parse("zz2a1b4c5d6e4f708192a3b4c5d6e7f8"));
	CHECK_FALSE(Guid::parse("3f2a1b4c-5d6e4f70-8192-a3b4c5d6e7f8x"));
	CHECK(Guid{}.isNull());
}

TEST_CASE("GUID: 重ならない、並べられる、ハッシュにできる")
{
	std::unordered_set<Guid> set;
	for (int i = 0; i < 10000; ++i)
		set.insert(Guid::generate());
	CHECK(set.size() == 10000);
	Guid lo, hi;
	hi.bytes[0] = 1;
	CHECK(lo < hi);
	CHECK(libraryVersion().major == 0);
}
