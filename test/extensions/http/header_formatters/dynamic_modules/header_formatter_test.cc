#include <string>
#include <thread>
#include <vector>

#include "source/extensions/http/header_formatters/dynamic_modules/header_formatter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace DynamicModules {
namespace {

using ::Envoy::StatusHelpers::IsOk;

// Loads a C test module by name from the test_data directory.
Extensions::DynamicModules::DynamicModulePtr loadModule(const std::string& name) {
  auto module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath(name, "c"), /*do_not_close=*/true);
  EXPECT_THAT(module.status(), IsOk());
  return std::move(module.value());
}

// The configuration destroys itself on the main thread dispatcher, so every test needs one. A real
// dispatcher is used rather than a mock so that the configurations are actually destroyed (when the
// dispatcher is torn down at the end of the test) instead of leaking.
class DynamicModuleHeaderFormatterTest : public testing::Test {
public:
  DynamicModuleHeaderFormatterTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_main_thread")) {}

  absl::StatusOr<DynamicModuleHeaderFormatterConfigSharedPtr>
  newFrom(const std::string& module_name, absl::string_view config = "") {
    return newDynamicModuleHeaderFormatterConfig("test_formatter", config, loadModule(module_name),
                                                 *dispatcher_);
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
};

// A missing ABI symbol is reported as NotFound so the factory can tell it apart from a
// configuration the module itself rejected.
TEST_F(DynamicModuleHeaderFormatterTest, MissingSymbolIsNotFound) {
  for (const auto& [module, symbol] : std::vector<std::pair<std::string, std::string>>{
           {"header_formatter_missing_config_new",
            "envoy_dynamic_module_on_header_formatter_config_new"},
           {"header_formatter_missing_config_destroy",
            "envoy_dynamic_module_on_header_formatter_config_destroy"},
           {"header_formatter_missing_new", "envoy_dynamic_module_on_header_formatter_new"},
           {"header_formatter_missing_destroy", "envoy_dynamic_module_on_header_formatter_destroy"},
           {"header_formatter_missing_process_key",
            "envoy_dynamic_module_on_header_formatter_process_key"},
           {"header_formatter_missing_format",
            "envoy_dynamic_module_on_header_formatter_format"}}) {
    auto result = newFrom(module);
    ASSERT_FALSE(result.ok()) << module;
    EXPECT_TRUE(absl::IsNotFound(result.status())) << module;
    EXPECT_THAT(std::string(result.status().message()), testing::HasSubstr(symbol));
  }
}

// An in-module init failure is reported as InvalidArgument.
TEST_F(DynamicModuleHeaderFormatterTest, ConfigNewNullIsInvalidArgument) {
  auto result = newFrom("header_formatter_config_new_fail");
  ASSERT_FALSE(result.ok());
  EXPECT_TRUE(absl::IsInvalidArgument(result.status()));
}

// A module that declines to create a formatter must not fail the message: create() returns nullptr
// and the codec falls back to Envoy's default header casing.
TEST_F(DynamicModuleHeaderFormatterTest, FormatterNewNullFallsBackToDefault) {
  auto config = newFrom("header_formatter_new_fail");
  ASSERT_THAT(config.status(), IsOk());
  EXPECT_EQ(nullptr, config.value()->create());
}

// A module whose format hook always declines leaves every key unchanged.
TEST_F(DynamicModuleHeaderFormatterTest, FormatDeclinedLeavesKeyUnchanged) {
  auto config = newFrom("header_formatter_no_op");
  ASSERT_THAT(config.status(), IsOk());
  auto formatter = config.value()->create();
  ASSERT_NE(nullptr, formatter);
  formatter->processKey("Content-Type");
  EXPECT_EQ("content-type", formatter->format("content-type"));
}

// The reason phrase is outside the module-facing ABI: it is dropped on the way in and reported as
// empty on the way out, which makes the codec use the canonical phrase for the response code.
TEST_F(DynamicModuleHeaderFormatterTest, ReasonPhraseIsNotForwarded) {
  auto config = newFrom("header_formatter_no_op");
  ASSERT_THAT(config.status(), IsOk());
  auto formatter = config.value()->create();
  ASSERT_NE(nullptr, formatter);
  formatter->setReasonPhrase("Totally Fine");
  EXPECT_EQ("", formatter->getReasonPhrase());
}

// The full processKey/format round trip: remembered keys come back with the peer's casing, and
// keys the module never saw take the other branch.
TEST_F(DynamicModuleHeaderFormatterTest, RestoresObservedCasing) {
  auto config = newFrom("header_formatter_preserve_case");
  ASSERT_THAT(config.status(), IsOk());
  auto formatter = config.value()->create();
  ASSERT_NE(nullptr, formatter);

  formatter->processKey("X-Foo-Bar");
  formatter->processKey("cOnTeNt-TyPe");

  EXPECT_EQ("X-Foo-Bar", formatter->format("x-foo-bar"));
  EXPECT_EQ("cOnTeNt-TyPe", formatter->format("content-type"));
  // Never observed, so the module falls through to its upper-casing branch.
  EXPECT_EQ("X-ENVOY-ADDED", formatter->format("x-envoy-added"));
}

// Each message gets its own in-module formatter, so keys observed by one must not leak into
// another created from the same configuration.
TEST_F(DynamicModuleHeaderFormatterTest, FormattersAreIndependent) {
  auto config = newFrom("header_formatter_preserve_case");
  ASSERT_THAT(config.status(), IsOk());

  auto first = config.value()->create();
  auto second = config.value()->create();
  ASSERT_NE(nullptr, first);
  ASSERT_NE(nullptr, second);

  first->processKey("X-Foo-Bar");
  EXPECT_EQ("X-Foo-Bar", first->format("x-foo-bar"));
  EXPECT_EQ("X-FOO-BAR", second->format("x-foo-bar"));
}

// The configuration is shared by every worker thread and create() is called once per message, so
// it must tolerate concurrent use.
TEST_F(DynamicModuleHeaderFormatterTest, CreateIsThreadSafe) {
  auto config = newFrom("header_formatter_preserve_case");
  ASSERT_THAT(config.status(), IsOk());

  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int i = 0; i < 8; i++) {
    threads.emplace_back([&config]() {
      for (int j = 0; j < 64; j++) {
        auto formatter = config.value()->create();
        ASSERT_NE(nullptr, formatter);
        formatter->processKey("X-Foo-Bar");
        EXPECT_EQ("X-Foo-Bar", formatter->format("x-foo-bar"));
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

// Whichever thread drops the last reference, the configuration - and with it the in-module destroy
// hook and the dlclose() of the module - is destroyed on the main thread. Losing this makes a
// straggler connection tear the module down from under a worker.
TEST_F(DynamicModuleHeaderFormatterTest, ConfigDestructionIsDeferredToMainThread) {
  testing::NiceMock<Event::MockDispatcher> main_thread_dispatcher;
  Event::DispatcherThreadDeletableConstPtr deleted;
  EXPECT_CALL(main_thread_dispatcher, deleteInDispatcherThread(testing::_))
      .WillOnce([&deleted](Event::DispatcherThreadDeletableConstPtr deletable) {
        deleted = std::move(deletable);
      });

  auto config = newDynamicModuleHeaderFormatterConfig(
      "test_formatter", "", loadModule("header_formatter_preserve_case"), main_thread_dispatcher);
  ASSERT_THAT(config.status(), IsOk());

  auto formatter = config.value()->create();
  ASSERT_NE(nullptr, formatter);
  config->reset();

  // Dropping the last reference from a worker thread hands the object to the main thread
  // dispatcher rather than destroying it in place.
  std::thread worker([&formatter]() { formatter.reset(); });
  worker.join();
  EXPECT_NE(nullptr, deleted);
}

// The formatter holds the configuration alive, so the module cannot be unloaded from under a
// formatter that outlives the last external reference to its config.
TEST_F(DynamicModuleHeaderFormatterTest, FormatterKeepsConfigAlive) {
  Envoy::Http::StatefulHeaderKeyFormatterPtr formatter;
  {
    auto config = newFrom("header_formatter_preserve_case");
    ASSERT_THAT(config.status(), IsOk());
    formatter = config.value()->create();
    ASSERT_NE(nullptr, formatter);
    formatter->processKey("X-Foo-Bar");
  }
  EXPECT_EQ("X-Foo-Bar", formatter->format("x-foo-bar"));
}

} // namespace
} // namespace DynamicModules
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
