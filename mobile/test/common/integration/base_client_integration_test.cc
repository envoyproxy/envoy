#include "test/common/integration/base_client_integration_test.h"

#include "source/extensions/http/header_formatters/preserve_case/config.h"
#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "test/common/http/common.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/common/config/internal.h"
#include "library/common/http/header_utility.h"

namespace Envoy {
namespace {

// Use the Envoy mobile default config as much as possible in this test.
// There are some config modifiers below which do result in deltas.
std::string defaultConfig() {
  Platform::EngineBuilder builder;
  std::string config_str = absl::StrCat(config_header, builder.generateConfigStr());
  return config_str;
}

} // namespace

Http::ResponseHeaderMapPtr toResponseHeaders(envoy_headers headers) {
  std::unique_ptr<Http::ResponseHeaderMapImpl> transformed_headers =
      Http::ResponseHeaderMapImpl::create();
  transformed_headers->setFormatter(
      std::make_unique<
          Extensions::Http::HeaderFormatters::PreserveCase::PreserveCaseHeaderFormatter>(
          false, envoy::extensions::http::header_formatters::preserve_case::v3::
                     PreserveCaseFormatterConfig::DEFAULT));
  Http::Utility::toEnvoyHeaders(*transformed_headers, headers);
  return transformed_headers;
}

BaseClientIntegrationTest::BaseClientIntegrationTest(Network::Address::IpVersion ip_version)
    : BaseIntegrationTest(ip_version, defaultConfig()) {
  use_lds_ = false;
  autonomous_upstream_ = true;
  defer_listener_finalization_ = true;

  HttpTestUtility::addDefaultHeaders(default_request_headers_);

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // The default stats config has overenthusiastic filters.
    bootstrap.clear_stats_config();
  });
}

void BaseClientIntegrationTest::initialize() {
  BaseIntegrationTest::initialize();

  bridge_callbacks_.context = &cc_;
  bridge_callbacks_.on_headers = [](envoy_headers c_headers, bool, envoy_stream_intel intel,
                                    void* context) -> void* {
    Http::ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
    callbacks_called* cc_ = static_cast<callbacks_called*>(context);
    cc_->on_headers_calls++;
    cc_->status = response_headers->Status()->value().getStringView();
    cc_->on_header_consumed_bytes_from_response = intel.consumed_bytes_from_response;
    return nullptr;
  };
  bridge_callbacks_.on_data = [](envoy_data c_data, bool, envoy_stream_intel,
                                 void* context) -> void* {
    callbacks_called* cc_ = static_cast<callbacks_called*>(context);
    cc_->on_data_calls++;
    release_envoy_data(c_data);
    return nullptr;
  };
  bridge_callbacks_.on_complete = [](envoy_stream_intel, envoy_final_stream_intel final_intel,
                                     void* context) -> void* {
    callbacks_called* cc_ = static_cast<callbacks_called*>(context);
    cc_->final_intel = final_intel;
    cc_->on_complete_received_byte_count = final_intel.received_byte_count;
    cc_->on_complete_calls++;
    cc_->terminal_callback->setReady();
    return nullptr;
  };
  bridge_callbacks_.on_error = [](envoy_error error, envoy_stream_intel, envoy_final_stream_intel,
                                  void* context) -> void* {
    release_envoy_error(error);
    callbacks_called* cc_ = static_cast<callbacks_called*>(context);
    cc_->on_error_calls++;
    cc_->terminal_callback->setReady();
    return nullptr;
  };
  bridge_callbacks_.on_cancel = [](envoy_stream_intel, envoy_final_stream_intel final_intel,
                                   void* context) -> void* {
    EXPECT_NE(-1, final_intel.stream_start_ms);
    callbacks_called* cc_ = static_cast<callbacks_called*>(context);
    cc_->on_cancel_calls++;
    cc_->terminal_callback->setReady();
    return nullptr;
  };

  ConditionalInitializer server_started;
  test_server_->server().dispatcher().post([this, &server_started]() -> void {
    http_client_ = std::make_unique<Http::Client>(
        test_server_->server().listenerManager().apiListener()->get().http()->get(), *dispatcher_,
        test_server_->statStore(), test_server_->server().api().randomGenerator());
    dispatcher_->drain(test_server_->server().dispatcher());
    server_started.setReady();
  });
  server_started.waitReady();
  default_request_headers_.setHost(fake_upstreams_[0]->localAddress()->asStringView());
}

void BaseClientIntegrationTest::cleanup() {
  if (xds_connection_ != nullptr) {
    cleanUpXdsConnection();
  }
  test_server_.reset();
  fake_upstreams_.clear();
}

} // namespace Envoy
