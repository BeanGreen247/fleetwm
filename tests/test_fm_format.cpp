#include <gtest/gtest.h>
#include <strings.h>

#include <random>
#include <string>
#include <vector>

#include "format.hpp"
#include "hasher.hpp"
#include "natural_sort.hpp"
#include "speed_meter.hpp"

using namespace fleetwm::fm;

TEST(FmFormat, SizesWordedLikeExplorer) {
  EXPECT_EQ(format_size(0), "0 bytes");
  EXPECT_EQ(format_size(1), "1 byte");
  EXPECT_EQ(format_size(1023), "1023 bytes");
  EXPECT_EQ(format_size(1024), "1.00 KB");
  EXPECT_EQ(format_size(1536), "1.50 KB");
  EXPECT_EQ(format_size(12345678), "11.8 MB");
  EXPECT_EQ(format_size(300ull << 20), "300 MB");
  EXPECT_EQ(format_size(5ull << 30), "5.00 GB");
}

TEST(FmFormat, DetailsColumnRoundsUpToWholeKilobytes) {
  EXPECT_EQ(format_size_kb(0), "0 KB");
  EXPECT_EQ(format_size_kb(1), "1 KB");
  EXPECT_EQ(format_size_kb(12345), "13 KB");
  EXPECT_EQ(format_size_kb(1048576), "1,024 KB");
}

TEST(FmFormat, ExactSizeShowsTheByteCount) {
  EXPECT_EQ(format_size_exact(500), "500 bytes");
  EXPECT_EQ(format_size_exact(1289748), "1.23 MB (1,289,748 bytes)");
  EXPECT_EQ(group_digits(0), "0");
  EXPECT_EQ(group_digits(999), "999");
  EXPECT_EQ(group_digits(1000), "1,000");
  EXPECT_EQ(group_digits(1234567890), "1,234,567,890");
}

TEST(FmFormat, SpeedAndRemainingTime) {
  EXPECT_EQ(format_speed(0), "0 KB/s");
  EXPECT_EQ(format_speed(500), "500 B/s");
  EXPECT_EQ(format_speed(30.0 * 1024 * 1024), "30.0 MB/s");
  EXPECT_EQ(format_remaining(-1), "Calculating...");
  EXPECT_EQ(format_remaining(0.2), "About 1 second");
  EXPECT_EQ(format_remaining(5), "About 5 seconds");
  EXPECT_EQ(format_remaining(59), "About 59 seconds");
  EXPECT_EQ(format_remaining(60), "About 1 minute");
  EXPECT_EQ(format_remaining(150), "About 3 minutes");
  EXPECT_EQ(format_remaining(3900), "About 1 hour 5 minutes");
  EXPECT_EQ(format_remaining(7200), "About 2 hours");
}

TEST(FmFormat, DatesInTheThreeStyles) {
  setenv("TZ", "UTC", 1);
  tzset();
  const time_t t = 1791519544;  // 2026-10-09 04:19:04 UTC
  EXPECT_EQ(format_date(t, DateStyle::Iso), "2026-10-09 04:19");
  EXPECT_EQ(format_date(t, DateStyle::Windows7), "10/9/2026 4:19 AM");
  EXPECT_EQ(format_date(t, DateStyle::European), "09.10.2026 04:19");
  EXPECT_EQ(format_date(t + 12 * 3600, DateStyle::Windows7), "10/9/2026 4:19 PM");
  EXPECT_EQ(format_date(t - 4 * 3600 - 19 * 60, DateStyle::Windows7), "10/9/2026 12:00 AM");
}

TEST(FmNaturalSort, NumbersCompareAsNumbers) {
  EXPECT_LT(natural_compare("file2", "file10"), 0);
  EXPECT_GT(natural_compare("file10", "file2"), 0);
  EXPECT_LT(natural_compare("a9b", "a10b"), 0);
  EXPECT_EQ(natural_compare("same", "same"), 0);
}

TEST(FmNaturalSort, CaseIsIgnoredExceptAsATieBreak) {
  EXPECT_LT(natural_compare("apple", "Banana"), 0);
  EXPECT_LT(natural_compare("Banana", "cherry"), 0);
  EXPECT_NE(natural_compare("abc", "ABC"), 0);
  EXPECT_LT(natural_compare("abc", "ABC"), 0);
}

TEST(FmNaturalSort, LeadingZerosAndPrefixes) {
  EXPECT_LT(natural_compare("a", "a1"), 0);
  EXPECT_LT(natural_compare("img1", "img01"), 0 + 1);  // equal value: a tie broken by zeros, not a reorder
  EXPECT_LT(natural_compare("img02", "img10"), 0);
  EXPECT_LT(natural_compare("", "a"), 0);
  EXPECT_EQ(natural_compare("", ""), 0);
}

TEST(FmHasher, KnownVectors) {
  Hasher h(HashAlgo::Sha256);
  h.update("abc", 3);
  EXPECT_EQ(h.finish_hex(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  Hasher m(HashAlgo::Md5);
  m.update("abc", 3);
  EXPECT_EQ(m.finish_hex(), "900150983cd24fb0d6963f7d28e17f72");
  Hasher s(HashAlgo::Sha1);
  s.update("a", 1);
  s.update("bc", 2);
  EXPECT_EQ(s.finish_hex(), "a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST(FmHasher, MoreAlgorithmsKnownVectors) {
  Hasher a(HashAlgo::Sha512);
  a.update("abc", 3);
  EXPECT_EQ(a.finish_hex(),
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
  Hasher b(HashAlgo::Blake2b);
  b.update("abc", 3);
  EXPECT_EQ(b.finish_hex(),
            "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923");
}

TEST(FmHasher, AutoPicksAConcreteStrongAlgorithmAndStaysWithIt) {
  const HashAlgo a = resolve_algo(HashAlgo::Auto);
  EXPECT_TRUE(a == HashAlgo::Sha256 || a == HashAlgo::Sha512 || a == HashAlgo::Blake2b);
  EXPECT_EQ(resolve_algo(HashAlgo::Auto), a) << "measured once";
  EXPECT_EQ(resolve_algo(HashAlgo::Md5), HashAlgo::Md5) << "an explicit choice is never changed";
  Hasher h(HashAlgo::Auto);
  h.update("abc", 3);
  Hasher g(a);
  g.update("abc", 3);
  EXPECT_EQ(h.finish_hex(), g.finish_hex());
  HashAlgo p;
  EXPECT_TRUE(parse_hash_name("BLAKE2b", &p));
  EXPECT_EQ(p, HashAlgo::Blake2b);
  EXPECT_TRUE(parse_hash_name("auto", &p));
  EXPECT_EQ(p, HashAlgo::Auto);
  EXPECT_STREQ(hash_label(HashAlgo::Sha512), "SHA-512");
}

TEST(FmHasher, NamesParse) {
  HashAlgo a;
  EXPECT_TRUE(parse_hash_name("SHA256", &a));
  EXPECT_EQ(a, HashAlgo::Sha256);
  EXPECT_TRUE(parse_hash_name("md5", &a));
  EXPECT_EQ(a, HashAlgo::Md5);
  EXPECT_FALSE(parse_hash_name("crc32", &a));
  EXPECT_STREQ(hash_name(HashAlgo::Sha1), "sha1");
}

TEST(FmSpeedMeter, SteadyRateGivesSteadyEstimate) {
  SpeedMeter m;
  for (int i = 0; i <= 40; ++i) m.update(i * 0.25, static_cast<uint64_t>(i * 0.25 * 10e6));
  EXPECT_NEAR(m.bytes_per_second(), 10e6, 1e5);
  EXPECT_NEAR(m.eta_seconds(100'000'000), 10.0, 0.2);
}

TEST(FmSpeedMeter, UnknownUntilItHasARate) {
  SpeedMeter m;
  EXPECT_LT(m.eta_seconds(1000), 0);
  m.update(0, 0);
  EXPECT_LT(m.eta_seconds(1000), 0);
}

TEST(FmSpeedMeter, ABurstFadesInsteadOfSticking) {
  SpeedMeter m;
  m.update(0, 0);
  m.update(1, 500'000'000);  // a cache burst
  const double burst = m.bytes_per_second();
  for (int i = 2; i < 30; ++i) m.update(i, 500'000'000 + (i - 1) * 1'000'000);  // then 1 MB/s
  EXPECT_LT(m.bytes_per_second(), burst / 20);
}

TEST(FmSpeedMeter, SamplesCloserThanFiftyMillisecondsAreFolded) {
  SpeedMeter m;
  m.update(0, 0);
  m.update(0.01, 1'000'000'000);
  EXPECT_EQ(m.bytes_per_second(), 0.0);
}

// The sort key must order names exactly as natural_compare does (ties by case and leading zeros aside).
TEST(FmNaturalSort, KeysOrderLikeTheComparator) {
  std::mt19937 rng(7);
  const std::string alphabet = "aAbBzZ09 -._/~éü1234567890";
  std::vector<std::string> names;
  for (int i = 0; i < 700; ++i) {
    std::string s;
    const int len = 1 + static_cast<int>(rng() % 9);
    for (int k = 0; k < len; ++k) s.push_back(alphabet[rng() % alphabet.size()]);
    names.push_back(s);
  }
  for (const char* s : {"file1", "file01", "file001", "File1", "file10", "file2", "a9", "a10", "", "0", "00", "000a", "00a", "1000000000000000000000", "999999999999999999999"}) names.push_back(s);
  std::vector<std::string> keys(names.size());
  for (size_t i = 0; i < names.size(); ++i) append_natural_key(names[i], &keys[i]);
  for (size_t i = 0; i < names.size(); ++i)
    for (size_t j = 0; j < names.size(); ++j) {
      const int want = natural_compare(names[i], names[j]);
      const int got = keys[i].compare(keys[j]);
      if (got != 0) EXPECT_EQ(got < 0, want < 0) << '"' << names[i] << "\" vs \"" << names[j] << '"';
      else EXPECT_TRUE(want == 0 || strcasecmp(names[i].c_str(), names[j].c_str()) == 0 || natural_compare(names[i], names[j]) != 0) << names[i] << " / " << names[j];
    }
}
