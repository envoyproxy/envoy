#include <string>
#include <vector>

#include "envoy/http/protocol.h"

#include "source/common/http/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine.h"
#include "library/cc/engine_builder.h"
#include "library/cc/stream.h"
#include "library/cc/stream_client.h"
#include "library/cc/stream_prototype.h"
#include "library/common/http/header_utility.h"

namespace Envoy {
namespace Platform {
namespace {

envoy_status_t fetchUrls(const std::vector<std::string> urls,
                         const std::vector<std::string> quic_hints,
                         std::vector<Http::Protocol>* used_protocols) {
  absl::Notification engine_running;
  Platform::EngineBuilder engine_builder;
  engine_builder.setLogLevel(Envoy::Logger::Logger::trace)
      .addRuntimeGuard("dns_cache_set_ip_version_to_remove", true)
      .addRuntimeGuard("quic_no_tcp_delay", true)
      .setOnEngineRunning([&engine_running]() { engine_running.Notify(); });
  for (const auto& quic_hint : quic_hints) {
    engine_builder.addQuicHint(quic_hint, 443);
  }
  Envoy::Platform::EngineSharedPtr engine = engine_builder.build();
  engine_running.WaitForNotification();

  for (const auto& url_string : urls) {
    Envoy::Http::Utility::Url url;
    if (!url.initialize(url_string, /*is_connect_request=*/false)) {
      std::cerr << "Unable to parse url: '" << url_string << "'\n";
      return ENVOY_FAILURE;
    }

    // Create stream.
    envoy_status_t status = ENVOY_SUCCESS;
    absl::Notification request_finished;
    Envoy::EnvoyStreamCallbacks stream_callbacks;
    stream_callbacks.on_complete_ = [&request_finished, used_protocols](
                                        envoy_stream_intel, envoy_final_stream_intel final_intel) {
      used_protocols->push_back(static_cast<Envoy::Http::Protocol>(final_intel.upstream_protocol));
      request_finished.Notify();
    };
    stream_callbacks.on_error_ = [&request_finished,
                                  &status](const Envoy::EnvoyError& error, envoy_stream_intel,
                                           envoy_final_stream_intel final_intel) {
      status = ENVOY_FAILURE;
      std::cerr << "Request failed after "
                << final_intel.stream_end_ms - final_intel.stream_start_ms
                << "ms with error message: " << error.message_ << "\n";
      request_finished.Notify();
    };
    Envoy::Platform::StreamSharedPtr stream =
        engine->streamClient()->newStreamPrototype()->start(std::move(stream_callbacks),
                                                            /*explicit_flow_control=*/false);

    auto headers = Envoy::Http::Utility::createRequestHeaderMapPtr();
    headers->addCopy(Envoy::Http::LowerCaseString(":method"), "GET");
    headers->addCopy(Envoy::Http::LowerCaseString(":scheme"), "https");
    headers->addCopy(Envoy::Http::LowerCaseString(":authority"), url.hostAndPort());
    headers->addCopy(Envoy::Http::LowerCaseString(":path"), url.pathAndQueryParams());
    stream->sendHeaders(std::move(headers), true);

    request_finished.WaitForNotification();

    if (status != ENVOY_SUCCESS) {
      return status;
    }
  }
  return ENVOY_SUCCESS;
}

TEST(FetchClientTest, Http2) {
  std::vector<Http::Protocol> protocols;
  ASSERT_EQ(fetchUrls({"https://www.google.com/"}, {}, &protocols), ENVOY_SUCCESS);
  ASSERT_EQ(protocols.front(), Http::Protocol::Http2);
}

TEST(FetchClientTest, Http3) {
  std::vector<Http::Protocol> protocols;
  ASSERT_EQ(fetchUrls({"https://www.google.com/", "https://www.google.com/"}, {"www.google.com"},
                      &protocols),
            ENVOY_SUCCESS);
  // The first request could either be HTTP/2 or HTTP/3 because we no longer give HTTP/3 a head
  // start.
  ASSERT_GE(protocols.at(0), Http::Protocol::Http2);
  // TODO(fredyw): In EngFlow CI, HTTP/3 does not work and will use HTTP/2 instead.
  ASSERT_GE(protocols.at(1), Http::Protocol::Http2);
}

} // namespace
} // namespace Platform
} // namespace Envoy
