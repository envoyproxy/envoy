#include "envoy/server/lifecycle_notifier.h"

#include "extensions/common/wasm/wasm.h"
#include "extensions/filters/network/wasm/wasm_filter.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/wasm_base.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::Context;
using Envoy::Extensions::Common::Wasm::Plugin;
using Envoy::Extensions::Common::Wasm::PluginSharedPtr;
using Envoy::Extensions::Common::Wasm::Wasm;
using proxy_wasm::AllowedCapabilitiesMap;
using proxy_wasm::ContextBase;

class TestFilter : public Context {
public:
  TestFilter(Wasm* wasm, uint32_t root_context_id, PluginSharedPtr plugin)
      : Context(wasm, root_context_id, plugin) {}
  MOCK_CONTEXT_LOG_;

  void testClose() { onCloseTCP(); }
};

class TestRoot : public Context {
public:
  TestRoot(Wasm* wasm, const std::shared_ptr<Plugin>& plugin) : Context(wasm, plugin) {}
  MOCK_CONTEXT_LOG_;
};

class WasmNetworkFilterTest : public Extensions::Common::Wasm::WasmNetworkFilterTestBase<
                                  testing::TestWithParam<std::tuple<std::string, std::string>>> {
public:
  WasmNetworkFilterTest() = default;
  ~WasmNetworkFilterTest() override = default;

  void setupConfig(const std::string& code, std::string vm_configuration, bool fail_open = false,
                   AllowedCapabilitiesMap allowed_capabilities = {}) {
    if (code.empty()) {
      setupWasmCode(vm_configuration);
    } else {
      code_ = code;
    }
    setVmConfiguration(vm_configuration);
    setFailOpen(fail_open);
    setAllowedCapabilities(allowed_capabilities);
    setupBase(std::get<0>(GetParam()), code_,
              [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
                return new TestRoot(wasm, plugin);
              });
  }

  void setupFilter() { setupFilterBase<TestFilter>(); }

  TestFilter& filter() { return *static_cast<TestFilter*>(context_.get()); }

private:
  void setupWasmCode(std::string vm_configuration) {
    if (std::get<0>(GetParam()) == "null") {
      code_ = "NetworkTestCpp";
    } else {
      if (std::get<1>(GetParam()) == "cpp") {
        code_ = TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
            "test/extensions/filters/network/wasm/test_data/test_cpp.wasm"));
      } else {
        code_ = TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(absl::StrCat(
            "test/extensions/filters/network/wasm/test_data/", vm_configuration + "_rust.wasm")));
      }
    }
    EXPECT_FALSE(code_.empty());
  }

protected:
  std::string code_;
};

INSTANTIATE_TEST_SUITE_P(RuntimesAndLanguages, WasmNetworkFilterTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_language_values);

// Bad code in initial config.
TEST_P(WasmNetworkFilterTest, BadCode) {
  setupConfig("bad code", "");
  EXPECT_EQ(wasm_, nullptr);
  setupFilter();
  filter().isFailed();
  EXPECT_CALL(read_filter_callbacks_.connection_,
              close(Envoy::Network::ConnectionCloseType::FlushWrite));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter().onNewConnection());
}

TEST_P(WasmNetworkFilterTest, BadCodeFailOpen) {
  setupConfig("bad code", "", true);
  EXPECT_EQ(wasm_, nullptr);
  setupFilter();
  filter().isFailed();
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onNewConnection());
}

// Test happy path.
TEST_P(WasmNetworkFilterTest, HappyPath) {
  setupConfig("", "logging");
  setupFilter();

  EXPECT_CALL(filter(), log_(spdlog::level::trace, Eq(absl::string_view("onNewConnection 2"))));
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onNewConnection());

  Buffer::OwnedImpl fake_downstream_data("Fake");
  EXPECT_CALL(filter(), log_(spdlog::level::trace,
                             Eq(absl::string_view("onDownstreamData 2 len=4 end_stream=0\nFake"))));
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onData(fake_downstream_data, false));
  EXPECT_EQ(fake_downstream_data.toString(), "write");

  Buffer::OwnedImpl fake_upstream_data("Done");
  EXPECT_CALL(filter(), log_(spdlog::level::trace,
                             Eq(absl::string_view("onUpstreamData 2 len=4 end_stream=1\nDone"))));
  EXPECT_CALL(filter(),
              log_(spdlog::level::trace, Eq(absl::string_view("onUpstreamConnectionClose 2 0"))));
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onWrite(fake_upstream_data, true));
  filter().onAboveWriteBufferHighWatermark();
  filter().onBelowWriteBufferLowWatermark();

  EXPECT_CALL(filter(),
              log_(spdlog::level::trace, Eq(absl::string_view("onDownstreamConnectionClose 2 1"))));
  read_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  // Noop.
  read_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  filter().testClose();
}

TEST_P(WasmNetworkFilterTest, CloseDownstreamFirst) {
  setupConfig("", "logging");
  setupFilter();

  EXPECT_CALL(filter(), log_(spdlog::level::trace, Eq(absl::string_view("onNewConnection 2"))));
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onNewConnection());

  EXPECT_CALL(filter(),
              log_(spdlog::level::trace, Eq(absl::string_view("onDownstreamConnectionClose 2 1"))));
  write_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  read_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
}

TEST_P(WasmNetworkFilterTest, CloseStream) {
  setupConfig("", "logging");
  setupFilter();

  // No Context, does nothing.
  filter().onEvent(Network::ConnectionEvent::RemoteClose);
  Buffer::OwnedImpl fake_upstream_data("Done");
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onWrite(fake_upstream_data, true));
  Buffer::OwnedImpl fake_downstream_data("Fake");
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onData(fake_downstream_data, false));

  // Create context.
  EXPECT_CALL(filter(), log_(spdlog::level::trace, Eq(absl::string_view("onNewConnection 2"))));
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onNewConnection());
  EXPECT_CALL(filter(),
              log_(spdlog::level::trace, Eq(absl::string_view("onDownstreamConnectionClose 2 2"))));

  filter().onEvent(static_cast<Network::ConnectionEvent>(9999)); // Does nothing.
  filter().onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(WasmNetworkFilterTest, SegvFailOpen) {
  if (std::get<0>(GetParam()) != "v8" || std::get<1>(GetParam()) != "cpp") {
    return;
  }
  setupConfig("", "logging", true);
  EXPECT_TRUE(plugin_->fail_open_);
  setupFilter();

  EXPECT_CALL(filter(), log_(spdlog::level::trace, Eq(absl::string_view("onNewConnection 2"))));
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onNewConnection());

  EXPECT_CALL(filter(), log_(spdlog::level::trace, Eq(absl::string_view("before segv"))));
  filter().onForeignFunction(0, 0);
  EXPECT_TRUE(wasm_->wasm()->isFailed());

  Buffer::OwnedImpl fake_downstream_data("Fake");
  // No logging expected.
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onData(fake_downstream_data, false));
}

TEST_P(WasmNetworkFilterTest, RestrictOnNewConnection) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  AllowedCapabilitiesMap allowed_capabilities = {
      {"proxy_on_context_create", proxy_wasm::SanitizationConfig()},
      {"proxy_get_property", proxy_wasm::SanitizationConfig()},
      {"proxy_log", proxy_wasm::SanitizationConfig()},
      {"proxy_on_new_connection", proxy_wasm::SanitizationConfig()}};
  setupConfig("", "logging", false, allowed_capabilities);
  setupFilter();

  // Expect this call, because proxy_on_new_connection is allowed
  EXPECT_CALL(filter(), log_(spdlog::level::trace, Eq(absl::string_view("onNewConnection 2"))));
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onNewConnection());

  // Do not expect this call, because proxy_on_downstream_connection_close is not allowed
  EXPECT_CALL(filter(),
              log_(spdlog::level::trace, Eq(absl::string_view("onDownstreamConnectionClose 2 1"))))
      .Times(0);
  read_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  // Noop.
  read_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  filter().testClose();
}

TEST_P(WasmNetworkFilterTest, RestrictOnDownstreamConnectionClose) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  AllowedCapabilitiesMap allowed_capabilities = {
      {"proxy_on_context_create", proxy_wasm::SanitizationConfig()},
      {"proxy_get_property", proxy_wasm::SanitizationConfig()},
      {"proxy_log", proxy_wasm::SanitizationConfig()},
      {"proxy_on_downstream_connection_close", proxy_wasm::SanitizationConfig()}};
  setupConfig("", "logging", false, allowed_capabilities);
  setupFilter();

  // Do not expect this call, because proxy_on_new_connection is not allowed
  EXPECT_CALL(filter(), log_(spdlog::level::trace, Eq(absl::string_view("onNewConnection 2"))))
      .Times(0);
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onNewConnection());

  // Expect this call, because proxy_on_downstream_connection_close allowed
  EXPECT_CALL(filter(),
              log_(spdlog::level::trace, Eq(absl::string_view("onDownstreamConnectionClose 2 1"))));
  read_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  // Noop.
  read_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  filter().testClose();
}

TEST_P(WasmNetworkFilterTest, RestrictLog) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  AllowedCapabilitiesMap allowed_capabilities = {
      {"proxy_on_context_create", proxy_wasm::SanitizationConfig()},
      {"proxy_get_property", proxy_wasm::SanitizationConfig()},
      {"proxy_on_new_connection", proxy_wasm::SanitizationConfig()},
      {"proxy_on_downstream_connection_close", proxy_wasm::SanitizationConfig()}};
  setupConfig("", "logging", false, allowed_capabilities);
  setupFilter();

  // Do not expect this call, because proxy_log is not allowed
  EXPECT_CALL(filter(), log_(spdlog::level::trace, Eq(absl::string_view("onNewConnection 2"))))
      .Times(0);
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onNewConnection());

  // Do not expect this call, because proxy_log is not allowed
  EXPECT_CALL(filter(),
              log_(spdlog::level::trace, Eq(absl::string_view("onDownstreamConnectionClose 2 1"))))
      .Times(0);
  read_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  // Noop.
  read_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  filter().testClose();
}

} // namespace Wasm
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
