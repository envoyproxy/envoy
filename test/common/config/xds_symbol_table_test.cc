#include <thread>
#include <vector>

#include "source/common/config/xds_symbol_table.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

TEST(XdsSymbolTableTest, LosslessRoundTrip) {
  auto table = XdsSymbolTable::create();

  std::vector<std::string> test_cases = {
      "simple_cluster",
      "cluster.with.dots",
      "cluster/with/slashes",
      "cluster_with_underscores-and-dashes",
      "xdstp://traffic-director-global.xds.googleapis.com/envoy.config.cluster.v3.Cluster/"
      "414017652406/default/"
      "cloud-internal-17-lb-europe-west1_cloud_17_ilb_1082309372360_4767540509642979888",
      "xdstp://auth/type/id?k1=v1&k2=v2:8080",
      "a/b/c/d/e/f/g",
      "://only_scheme",
      "",
  };

  for (const auto& original : test_cases) {
    auto storage = table->encode(original);
    XdsResourceName name = storage.resourceName();
    std::string decoded = table->toString(name);
    EXPECT_EQ(original, decoded);
  }
}

TEST(XdsSymbolTableTest, EqualityAndHash) {
  auto table = XdsSymbolTable::create();

  std::string str1 = "xdstp://authority/envoy.config.cluster.v3.Cluster/project/cluster1";
  std::string str2 = "xdstp://authority/envoy.config.cluster.v3.Cluster/project/cluster1";
  std::string str3 = "xdstp://authority/envoy.config.cluster.v3.Cluster/project/cluster2";

  auto storage1 = table->encode(str1);
  auto storage2 = table->encode(str2);
  auto storage3 = table->encode(str3);

  EXPECT_EQ(storage1.resourceName(), storage2.resourceName());
  EXPECT_NE(storage1.resourceName(), storage3.resourceName());

  EXPECT_EQ(absl::Hash<XdsResourceName>()(storage1.resourceName()),
            absl::Hash<XdsResourceName>()(storage2.resourceName()));
}

TEST(XdsSymbolTableTest, ByteCompressionRatio) {
  auto table = XdsSymbolTable::create();

  std::string long_name =
      "xdstp://traffic-director-global.xds.googleapis.com/envoy.config.cluster.v3.Cluster/"
      "414017652406/default/"
      "cloud-internal-17-lb-europe-west1_cloud_17_ilb_1082309372360_4767540509642979888";

  auto storage = table->encode(long_name);
  EXPECT_GT(long_name.size(), 170);
  // Encoded bytes should be roughly 40-50 bytes vs 184 bytes raw string
  EXPECT_LE(storage.resourceName().size(), 55);
  EXPECT_GT(storage.resourceName().size(), 10);

  // Compression ratio should be > 3x on raw bytes alone
  double ratio = static_cast<double>(long_name.size()) / storage.resourceName().size();
  EXPECT_GT(ratio, 3.0);
}

TEST(XdsSymbolTableTest, RefCountReclamation) {
  auto table = XdsSymbolTable::create();
  const size_t initial_active = table->numActiveSymbols();

  {
    auto storage = table->encode("unique_token");
    EXPECT_GT(table->numActiveSymbols(), initial_active);
  }

  // After storage goes out of scope, the token should be reclaimed from encode_map_
  EXPECT_EQ(table->numActiveSymbols(), initial_active);

  // Re-encoding should reuse symbol slot from free_list_
  const size_t symbols_before = table->numSymbols();
  auto storage2 = table->encode("another_token");
  // Total allocated symbols in table does not grow because it reused from free_list_
  EXPECT_LE(table->numSymbols(), symbols_before);
  EXPECT_EQ("another_token", table->toString(storage2.resourceName()));
}

TEST(XdsSymbolTableTest, StorageCopyMoveAndOrdering) {
  auto table = XdsSymbolTable::create();

  XdsResourceName empty_name;
  EXPECT_TRUE(empty_name.empty());
  EXPECT_EQ(empty_name.data(), nullptr);
  EXPECT_EQ(empty_name.size(), 0);

  XdsResourceNameStorage null_table_storage(nullptr, "ignored");
  EXPECT_EQ(null_table_storage.size(), 0);

  XdsResourceNameStorage s1(table, "alpha/one");
  EXPECT_GT(s1.size(), 0);
  EXPECT_EQ("alpha/one", table->toString(s1.resourceName()));

  // Copy constructor and copy assignment.
  XdsResourceNameStorage s2(s1);
  EXPECT_EQ(s1.resourceName(), s2.resourceName());
  XdsResourceNameStorage s3(table, "beta/two");
  EXPECT_TRUE(s1.resourceName() < s3.resourceName() || s3.resourceName() < s1.resourceName());
  EXPECT_FALSE(s1.resourceName() < s1.resourceName());
  EXPECT_FALSE(empty_name < XdsResourceName());
  EXPECT_TRUE(empty_name < s1.resourceName());

  s3 = s1;
  EXPECT_EQ(s1.resourceName(), s3.resourceName());
  s3 = null_table_storage;
  EXPECT_EQ(s3.size(), 0);

  // Move constructor and move assignment.
  XdsResourceNameStorage s4(std::move(s2));
  EXPECT_EQ("alpha/one", table->toString(s4.resourceName()));
  XdsResourceNameStorage s5(table, "gamma/three");
  s5 = std::move(s4);
  EXPECT_EQ("alpha/one", table->toString(s5.resourceName()));

  // Exercise symbol-span overloads (toString, addRef, releaseRef).
  std::vector<XdsSymbol> symbols = XdsSymbolTable::decodeBytesToSymbols(s5.resourceName().span());
  EXPECT_EQ("alpha/one", table->toString(symbols));
  table->addRef(symbols);
  table->releaseRef(symbols);
}

TEST(XdsSymbolTableTest, MultiByteLeb128Symbols) {
  auto table = XdsSymbolTable::create();
  std::vector<XdsResourceNameStorage> storages;
  storages.reserve(200);
  for (int i = 0; i < 200; ++i) {
    storages.emplace_back(table->encode("token" + std::to_string(i) + "/suffix"));
  }
  for (int i = 0; i < 200; ++i) {
    EXPECT_EQ("token" + std::to_string(i) + "/suffix", table->toString(storages[i].resourceName()));
  }
}

TEST(XdsSymbolTableTest, ConcurrentAccessThreadSafe) {
  auto table = XdsSymbolTable::create();
  const size_t num_threads = 8;
  const size_t iterations = 500;
  const size_t initial_active = table->numActiveSymbols();

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  for (size_t t = 0; t < num_threads; ++t) {
    threads.emplace_back([table, t]() {
      for (size_t i = 0; i < iterations; ++i) {
        std::string name =
            absl::StrCat("xdstp://authority.org/envoy.config.cluster.v3.Cluster/thread_", t,
                         "/cluster_", i % 20);
        auto storage = table->encode(name);
        XdsResourceName res_name = storage.resourceName();
        std::string decoded = table->toString(res_name);
        EXPECT_EQ(name, decoded);

        // Copy and move storage across variables to exercise addRef and releaseRef concurrently.
        auto copy_storage = storage;
        EXPECT_EQ(copy_storage.resourceName(), res_name);
        auto moved_storage = std::move(copy_storage);
        EXPECT_EQ(moved_storage.resourceName(), res_name);
      }
    });
  }

  for (auto& th : threads) {
    th.join();
  }

  // Once all threads finish and storage instances go out of scope, active symbols must return to
  // initial state.
  EXPECT_EQ(table->numActiveSymbols(), initial_active);
}

TEST(XdsSymbolTableTest, MalformedLeb128ByteSpansSafelyRejected) {
  // Test truncated single byte with `MSB` set.
  const uint8_t truncated1[] = {0x80};
  EXPECT_TRUE(XdsSymbolTable::decodeBytesToSymbols(truncated1).empty());

  // Test truncated multi-byte sequence where second byte has `MSB` set at end of span.
  const uint8_t truncated2[] = {0x01, 0x85};
  EXPECT_TRUE(XdsSymbolTable::decodeBytesToSymbols(truncated2).empty());

  // Test shift overflow / infinite continuation bytes (> 5 bytes).
  const uint8_t excessive_shift[] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01};
  EXPECT_TRUE(XdsSymbolTable::decodeBytesToSymbols(excessive_shift).empty());

  // Test 32-bit overflow on byte 5 (bits 4-6 set at shift 28).
  const uint8_t bit_overflow[] = {0x80, 0x80, 0x80, 0x80, 0x70};
  EXPECT_TRUE(XdsSymbolTable::decodeBytesToSymbols(bit_overflow).empty());

  // Test valid max 32-bit integer: 0xFFFFFFFF -> 5 bytes with 0x0F on byte 5.
  const uint8_t max_u32[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
  auto syms = XdsSymbolTable::decodeBytesToSymbols(max_u32);
  ASSERT_EQ(syms.size(), 1);
  EXPECT_EQ(syms[0], 0xFFFFFFFF);
}

TEST(XdsResourceNameTest, EmptyAndDefaultConstructedSafety) {
  auto table = XdsSymbolTable::create();
  XdsResourceName empty_name;
  const XdsResourceName other_empty;

  EXPECT_TRUE(empty_name.empty());
  EXPECT_EQ(empty_name.size(), 0);
  EXPECT_EQ(empty_name.data(), nullptr);
  EXPECT_TRUE(empty_name.span().empty());
  EXPECT_EQ(table->toString(empty_name), "");

  // Reflexive equality and comparison.
  EXPECT_EQ(empty_name, other_empty);
  EXPECT_FALSE(empty_name != other_empty);
  EXPECT_FALSE(empty_name < other_empty);

  // Hashing default-constructed name must not crash.
  EXPECT_EQ(absl::Hash<XdsResourceName>()(empty_name),
            absl::Hash<XdsResourceName>()(XdsResourceName()));

  // Compare empty with non-empty.
  auto storage = table->encode("cluster.foo");
  XdsResourceName non_empty = storage.resourceName();
  EXPECT_FALSE(non_empty.empty());
  EXPECT_NE(empty_name, non_empty);
  EXPECT_TRUE(empty_name < non_empty);
  EXPECT_FALSE(non_empty < empty_name);
}

TEST(XdsSymbolTableTest, ForEachSymbolInlineVisitor) {
  auto table = XdsSymbolTable::create();
  auto storage = table->encode("cluster.with.dots");
  XdsResourceName name = storage.resourceName();

  size_t count = 0;
  bool ok = XdsSymbolTable::forEachSymbol(name.span(), [&count](XdsSymbol sym) {
    EXPECT_GT(sym, 0);
    ++count;
  });
  EXPECT_TRUE(ok);
  // "cluster" (1), "." (1), "with" (1), "." (1), "dots" (1) -> 5 symbols
  EXPECT_EQ(count, 5);

  // Visitor on empty span.
  count = 0;
  ok = XdsSymbolTable::forEachSymbol({}, [&count](XdsSymbol) { ++count; });
  EXPECT_TRUE(ok);
  EXPECT_EQ(count, 0);

  // Visitor on malformed byte stream fails safely.
  const uint8_t malformed[] = {0x80};
  count = 0;
  ok = XdsSymbolTable::forEachSymbol(malformed, [&count](XdsSymbol) { ++count; });
  EXPECT_FALSE(ok);
  EXPECT_EQ(count, 0);
}

} // namespace
} // namespace Config
} // namespace Envoy
