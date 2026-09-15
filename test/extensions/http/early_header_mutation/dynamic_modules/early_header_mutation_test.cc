#include <thread>
#include <vector>

#include "source/extensions/http/early_header_mutation/dynamic_modules/early_header_mutation.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace DynamicModules {
namespace {

using ::Envoy::StatusHelpers::IsOk;
using ::testing::Not;

// Loads a C test module by name from the test_data directory.
Extensions::DynamicModules::DynamicModulePtr loadModule(const std::string& name) {
  auto module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath(name, "c"), /*do_not_close=*/true);
  EXPECT_THAT(module.status(), IsOk());
  return std::move(module.value());
}

absl::StatusOr<DynamicModuleEarlyHeaderMutationPtr> newFrom(const std::string& module_name,
                                                            absl::string_view config = "") {
  return newDynamicModuleEarlyHeaderMutation("test_mutation", config, loadModule(module_name));
}

// A missing ABI symbol is reported as NotFound so the factory can attribute it to
// module_load_error. config_test.cc depends on this mapping.
TEST(DynamicModuleEarlyHeaderMutationTest, MissingSymbolIsNotFound) {
  for (const auto& [module, symbol] : std::vector<std::pair<std::string, std::string>>{
           {"early_header_mutation_missing_config_new",
            "envoy_dynamic_module_on_early_header_mutation_config_new"},
           {"early_header_mutation_missing_config_destroy",
            "envoy_dynamic_module_on_early_header_mutation_config_destroy"},
           {"early_header_mutation_missing_mutate",
            "envoy_dynamic_module_on_early_header_mutation_mutate"}}) {
    auto result = newFrom(module);
    ASSERT_FALSE(result.ok()) << module;
    EXPECT_TRUE(absl::IsNotFound(result.status())) << module;
    EXPECT_THAT(std::string(result.status().message()), testing::HasSubstr(symbol));
  }
}

// An in-module init failure is reported as InvalidArgument so the factory attributes it to
// config_init_error instead.
TEST(DynamicModuleEarlyHeaderMutationTest, ConfigNewNullIsInvalidArgument) {
  auto result = newFrom("early_header_mutation_config_new_fail");
  ASSERT_FALSE(result.ok());
  EXPECT_TRUE(absl::IsInvalidArgument(result.status()));
}

TEST(DynamicModuleEarlyHeaderMutationTest, MutateReturnsTrueContinuesChain) {
  auto extension = newFrom("early_header_mutation_no_op");
  ASSERT_THAT(extension.status(), IsOk());
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_TRUE(extension.value()->mutate(headers, stream_info));
}

TEST(DynamicModuleEarlyHeaderMutationTest, MutateReturnsFalseStopsChain) {
  auto extension = newFrom("early_header_mutation_stop_chain");
  ASSERT_THAT(extension.status(), IsOk());
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_FALSE(extension.value()->mutate(headers, stream_info));
  // Mutations applied before the module stopped the chain are kept.
  EXPECT_EQ("ran", headers.get_("x-dynamic-module-first"));
}

TEST(DynamicModuleEarlyHeaderMutationTest, MutateRewritesHeaders) {
  auto extension = newFrom("early_header_mutation_rewrite");
  ASSERT_THAT(extension.status(), IsOk());
  Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {"x-test-input", "hello"}, {"x-remove-me", "bye"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_TRUE(extension.value()->mutate(headers, stream_info));

  // set_header echoed the value read back through get_header_value.
  EXPECT_EQ("hello", headers.get_("x-dynamic-module-echo"));
  // add_header built a multi-value header and get_header_value reported the count.
  EXPECT_EQ(2, headers.get(Envoy::Http::LowerCaseString("x-dynamic-module-added")).size());
  EXPECT_EQ("2", headers.get_("x-dynamic-module-multi"));
  // remove_header removed the key.
  EXPECT_TRUE(headers.get(Envoy::Http::LowerCaseString("x-remove-me")).empty());
  EXPECT_EQ("yes", headers.get_("x-dynamic-module-removed"));
}

// The extension is const and reused across requests, so it must keep no per-request state.
TEST(DynamicModuleEarlyHeaderMutationTest, MutateIsReentrant) {
  auto extension = newFrom("early_header_mutation_rewrite");
  ASSERT_THAT(extension.status(), IsOk());
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  Envoy::Http::TestRequestHeaderMapImpl first{{":method", "GET"}, {"x-test-input", "one"}};
  Envoy::Http::TestRequestHeaderMapImpl second{{":method", "GET"}, {"x-test-input", "two"}};
  EXPECT_TRUE(extension.value()->mutate(first, stream_info));
  EXPECT_TRUE(extension.value()->mutate(second, stream_info));
  EXPECT_EQ("one", first.get_("x-dynamic-module-echo"));
  EXPECT_EQ("two", second.get_("x-dynamic-module-echo"));
}

// One extension instance is shared by every worker thread, so mutate() must be safe to call
// concurrently. This is the test that catches someone later adding mutable state to the class.
TEST(DynamicModuleEarlyHeaderMutationTest, MutateIsConcurrent) {
  auto extension = newFrom("early_header_mutation_rewrite");
  ASSERT_THAT(extension.status(), IsOk());
  const auto& shared = *extension.value();

  constexpr int thread_count = 8;
  std::vector<std::thread> threads;
  std::vector<Envoy::Http::TestRequestHeaderMapImpl> header_maps;
  header_maps.reserve(thread_count);
  for (int i = 0; i < thread_count; i++) {
    header_maps.push_back(Envoy::Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                {"x-test-input", absl::StrCat(i)}});
  }

  threads.reserve(thread_count);
  for (int i = 0; i < thread_count; i++) {
    threads.emplace_back([&shared, &header_maps, i]() {
      NiceMock<StreamInfo::MockStreamInfo> stream_info;
      EXPECT_TRUE(shared.mutate(header_maps[i], stream_info));
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  for (int i = 0; i < thread_count; i++) {
    EXPECT_EQ(absl::StrCat(i), header_maps[i].get_("x-dynamic-module-echo"));
  }
}

} // namespace
} // namespace DynamicModules
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
