#include "envoy/network/transport_socket.h"
#include "envoy/server/lifecycle_notifier.h"

#include "source/extensions/common/wasm/wasm.h"
#include "source/extensions/filters/network/wasm/wasm_filter.h"

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
  TestFilter(Wasm* wasm, uint32_t root_context_id,
             Common::Wasm::PluginHandleSharedPtr plugin_handle)
      : Context(wasm, root_context_id, plugin_handle) {}
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

  void setupConfig(const std::string& code, std::string root_id = "",
                   std::string vm_configuration = "", bool fail_open = false,
                   proxy_wasm::AllowedCapabilitiesMap allowed_capabilities = {}) {
    if (code.empty()) {
      setupWasmCode(root_id, vm_configuration);
    } else {
      code_ = code;
    }
    setRootId(root_id);
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
  void setupWasmCode(std::string root_id, std::string vm_configuration) {
    if (std::get<0>(GetParam()) == "null") {
      code_ = "NetworkTestCpp";
    } else {
      if (std::get<1>(GetParam()) == "cpp") {
        code_ = TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
            "test/extensions/filters/network/wasm/test_data/test_cpp.wasm"));
      } else {
        auto filename = !root_id.empty() ? root_id : vm_configuration;
        code_ = TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(absl::StrCat(
            "test/extensions/filters/network/wasm/test_data/", filename + "_rust.wasm")));
      }
    }
    EXPECT_FALSE(code_.empty());
  }

protected:
  std::string code_;
};

INSTANTIATE_TEST_SUITE_P(RuntimesAndLanguages, WasmNetworkFilterTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_language_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);

// Bad code in initial config.
TEST_P(WasmNetworkFilterTest, BadCode) {
  setupConfig("bad code");
  EXPECT_EQ(wasm_, nullptr);
  setupFilter();
  filter().isFailed();
  EXPECT_CALL(read_filter_callbacks_.connection_,
              close(Envoy::Network::ConnectionCloseType::FlushWrite));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter().onNewConnection());
}

TEST_P(WasmNetworkFilterTest, BadCodeFailOpen) {
  setupConfig("bad code", "", "", true);
  EXPECT_EQ(wasm_, nullptr);
  setupFilter();
  filter().isFailed();
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onNewConnection());
}

// Test happy path.
TEST_P(WasmNetworkFilterTest, HappyPath) {
  setupConfig("", "", "logging");
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
  setupConfig("", "", "logging");
  setupFilter();

  EXPECT_CALL(filter(), log_(spdlog::level::trace, Eq(absl::string_view("onNewConnection 2"))));
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onNewConnection());

  EXPECT_CALL(filter(),
              log_(spdlog::level::trace, Eq(absl::string_view("onDownstreamConnectionClose 2 1"))));
  write_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  read_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
}

TEST_P(WasmNetworkFilterTest, CloseStream) {
  setupConfig("", "", "logging");
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
  setupConfig("", "", "logging", true);
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
  setupConfig("", "", "logging", false, allowed_capabilities);
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
  setupConfig("", "", "logging", false, allowed_capabilities);
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
  setupConfig("", "", "logging", false, allowed_capabilities);
  setupFilter();

  // Do not expect this call, because proxy_log is not allowed
  EXPECT_CALL(filter(), log_(spdlog::level::trace, Eq(absl::string_view("onNewConnection 2"))))
      .Times(0);
  if (std::get<1>(GetParam()) == "rust") {
    // Rust code panics on WasmResult::InternalFailure returned by restricted calls, and
    // that eventually results in calling failStream on post VM failure check after onNewConnection.
    EXPECT_EQ(Network::FilterStatus::StopIteration, filter().onNewConnection());
  } else {
    EXPECT_EQ(Network::FilterStatus::Continue, filter().onNewConnection());
  }
  // Do not expect this call, because proxy_log is not allowed
  EXPECT_CALL(filter(),
              log_(spdlog::level::trace, Eq(absl::string_view("onDownstreamConnectionClose 2 1"))))
      .Times(0);
  read_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  // Noop.
  read_filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  filter().testClose();
}

TEST_P(WasmNetworkFilterTest, StopAndResumeDownstreamViaAsyncCall) {
  setupConfig("", "resume_call");
  setupFilter();

  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks = nullptr;
  cluster_manager_.initializeThreadLocalClusters({"cluster"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                      {":path", "/"},
                                                      {":authority", "foo"},
                                                      {"content-length", "6"}}),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));

  EXPECT_CALL(filter(), log_(spdlog::level::trace, Eq(absl::string_view("onDownstreamData 2"))))
      .WillOnce(Invoke([&](uint32_t, absl::string_view) -> proxy_wasm::WasmResult {
        Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
            Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
        NiceMock<Tracing::MockSpan> span;
        Http::TestResponseHeaderMapImpl response_header{{":status", "200"}};
        callbacks->onBeforeFinalizeUpstreamSpan(span, &response_header);
        callbacks->onSuccess(request, std::move(response_message));
        return proxy_wasm::WasmResult::Ok;
      }));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("continueDownstream"))));

  EXPECT_CALL(read_filter_callbacks_, continueReading()).WillOnce(Invoke([&]() {
    // Verify that we're not resuming processing from within Wasm callback.
    EXPECT_EQ(proxy_wasm::current_context_, nullptr);
  }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter().onNewConnection());
  Buffer::OwnedImpl fake_downstream_data("");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter().onData(fake_downstream_data, false));

  EXPECT_NE(callbacks, nullptr);
}

TEST_P(WasmNetworkFilterTest, StopAndResumeUpstreamViaAsyncCall) {
  setupConfig("", "resume_call");
  setupFilter();

  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks = nullptr;
  cluster_manager_.initializeThreadLocalClusters({"cluster"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                      {":path", "/"},
                                                      {":authority", "foo"},
                                                      {"content-length", "6"}}),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));

  EXPECT_CALL(filter(), log_(spdlog::level::trace, Eq(absl::string_view("onUpstreamData 2"))))
      .WillOnce(Invoke([&](uint32_t, absl::string_view) -> proxy_wasm::WasmResult {
        Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
            Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
        NiceMock<Tracing::MockSpan> span;
        Http::TestResponseHeaderMapImpl response_header{{":status", "200"}};
        callbacks->onBeforeFinalizeUpstreamSpan(span, &response_header);
        callbacks->onSuccess(request, std::move(response_message));
        return proxy_wasm::WasmResult::Ok;
      }));

  if (std::get<1>(GetParam()) != "rust") {
    // Rust SDK panics on Status::Unimplemented and other non-recoverable errors.
    EXPECT_CALL(filter(),
                log_(spdlog::level::info, Eq(absl::string_view("continueUpstream unimplemented"))));
  }

  EXPECT_EQ(Network::FilterStatus::Continue, filter().onNewConnection());
  Buffer::OwnedImpl fake_upstream_data("");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter().onWrite(fake_upstream_data, false));

  EXPECT_NE(callbacks, nullptr);
}

TEST_P(WasmNetworkFilterTest, PanicOnNewConnection) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  setupConfig("", "panic");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::critical, _))
      .Times(testing::AtMost(1)); // Rust logs on panic.
  EXPECT_EQ(read_filter_callbacks_.connection().state(), Network::Connection::State::Open);
  EXPECT_EQ(write_filter_callbacks_.connection().state(), Network::Connection::State::Open);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter().onNewConnection());

  // Should close both up and down streams.
  EXPECT_EQ(read_filter_callbacks_.connection().state(), Network::Connection::State::Closed);
  EXPECT_EQ(write_filter_callbacks_.connection().state(), Network::Connection::State::Closed);
}

TEST_P(WasmNetworkFilterTest, PanicOnDownstreamData) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  setupConfig("", "panic");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::critical, _))
      .Times(testing::AtMost(1)); // Rust logs on panic.
  EXPECT_EQ(read_filter_callbacks_.connection().state(), Network::Connection::State::Open);
  EXPECT_EQ(write_filter_callbacks_.connection().state(), Network::Connection::State::Open);
  Buffer::OwnedImpl fake_downstream_data("Fake");
  filter().onCreate(); // Create context without calling OnNewConnection.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter().onData(fake_downstream_data, false));

  // Should close both up and down streams.
  EXPECT_EQ(read_filter_callbacks_.connection().state(), Network::Connection::State::Closed);
  EXPECT_EQ(write_filter_callbacks_.connection().state(), Network::Connection::State::Closed);
}

TEST_P(WasmNetworkFilterTest, PanicOnUpstreamData) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  setupConfig("", "panic");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::critical, _))
      .Times(testing::AtMost(1)); // Rust logs on panic.
  EXPECT_EQ(read_filter_callbacks_.connection().state(), Network::Connection::State::Open);
  EXPECT_EQ(write_filter_callbacks_.connection().state(), Network::Connection::State::Open);
  Buffer::OwnedImpl fake_downstream_data("Fake");
  filter().onCreate(); // Create context without calling OnNewConnection.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter().onWrite(fake_downstream_data, false));

  // Should close both up and down streams.
  EXPECT_EQ(read_filter_callbacks_.connection().state(), Network::Connection::State::Closed);
  EXPECT_EQ(write_filter_callbacks_.connection().state(), Network::Connection::State::Closed);
}

TEST_P(WasmNetworkFilterTest, CloseDownstream) {
  setupConfig("", "close_stream");
  setupFilter();
  EXPECT_EQ(read_filter_callbacks_.connection().state(), Network::Connection::State::Open);
  EXPECT_EQ(write_filter_callbacks_.connection().state(), Network::Connection::State::Open);
  Buffer::OwnedImpl fake_downstream_data("Fake");
  filter().onCreate(); // Create context without calling OnNewConnection.
  EXPECT_CALL(read_filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWrite, "wasm_downstream_close"));
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onData(fake_downstream_data, false));

  // Should close downstream.
  EXPECT_EQ(read_filter_callbacks_.connection().state(), Network::Connection::State::Closed);
  EXPECT_EQ(write_filter_callbacks_.connection().state(), Network::Connection::State::Open);
}

TEST_P(WasmNetworkFilterTest, CloseUpstream) {
  setupConfig("", "close_stream");
  setupFilter();
  EXPECT_EQ(read_filter_callbacks_.connection().state(), Network::Connection::State::Open);
  EXPECT_EQ(write_filter_callbacks_.connection().state(), Network::Connection::State::Open);
  Buffer::OwnedImpl fake_upstream_data("Fake");
  filter().onCreate(); // Create context without calling OnNewConnection.
  EXPECT_CALL(write_filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWrite, "wasm_upstream_close"));
  EXPECT_EQ(Network::FilterStatus::Continue, filter().onWrite(fake_upstream_data, false));

  // Should close upstream.
  EXPECT_EQ(read_filter_callbacks_.connection().state(), Network::Connection::State::Open);
  EXPECT_EQ(write_filter_callbacks_.connection().state(), Network::Connection::State::Closed);
}

} // namespace Wasm
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
