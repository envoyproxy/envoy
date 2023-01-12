#include "source/common/http/conn_manager_utility.h"
#include "source/common/http/exception.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/http1/codec_impl.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/test_runtime.h"

#include "absl/strings/substitute.h"
#include "gmock/gmock.h"

using testing::_;
using testing::Invoke;
using testing::InvokeWithoutArgs;

namespace Envoy {
namespace Http {
namespace Http1 {
namespace {

static Http1Settings fromHttp1Settings() {
  Http1Settings h1_settings;
  h1_settings.allow_absolute_url_ = true;
  h1_settings.accept_http_10_ = true;
  h1_settings.default_host_for_http_10_ = "localhost";
  h1_settings.enable_trailers_ = true;
  h1_settings.stream_error_on_invalid_http_message_ = true;
  return h1_settings;
}

class Http1Harness {
public:
  Http1Harness(const Http1Settings& server_settings, const Http1Settings& client_settings)
      : server_settings(server_settings), client_settings(client_settings) {
    ON_CALL(mock_server_callbacks, newStream(_, _))
        .WillByDefault(Invoke(
            [&](ResponseEncoder&, bool) -> RequestDecoder& { return orphan_request_decoder; }));
  }

  void fuzz_request(Buffer::Instance& payload) {
    client_ = std::make_unique<Http1::ClientConnectionImpl>(
        mock_client_connection, Http1::CodecStats::atomicGet(http1_stats, stats_store),
        mock_client_callbacks, client_settings, Http::DEFAULT_MAX_HEADERS_COUNT);
    Status status = client_->dispatch(payload);
  }
  void fuzz_response(Buffer::Instance& payload) {
    server_ = std::make_unique<Http1::ServerConnectionImpl>(
        mock_server_connection, Http1::CodecStats::atomicGet(http1_stats, stats_store),
        mock_server_callbacks, server_settings, Http::DEFAULT_MAX_REQUEST_HEADERS_KB,
        Http::DEFAULT_MAX_HEADERS_COUNT, envoy::config::core::v3::HttpProtocolOptions::ALLOW);

    Status status = server_->dispatch(payload);
  }

private:
  const Http1Settings server_settings, client_settings;
  Stats::IsolatedStoreImpl stats_store;
  Http1::CodecStats::AtomicPtr http1_stats;

  NiceMock<MockConnectionCallbacks> mock_client_callbacks;
  NiceMock<Network::MockConnection> mock_client_connection;
  ClientConnectionPtr client_;

  NiceMock<MockRequestDecoder> orphan_request_decoder;
  NiceMock<Network::MockConnection> mock_server_connection;
  NiceMock<MockServerConnectionCallbacks> mock_server_callbacks;

  ServerConnectionPtr server_;
};

static std::unique_ptr<Http1Harness> harness;
static void reset_harness() { harness = nullptr; }

// Fuzzing strategy
// Unconstrained fuzzing, rely on corpus for coverage

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  if (harness == nullptr) {
    Http1Settings server_settings = fromHttp1Settings();
    Http1Settings client_settings = fromHttp1Settings();
    harness = std::make_unique<Http1Harness>(server_settings, client_settings);
    atexit(reset_harness);
  }

  Buffer::OwnedImpl httpmsg;
  httpmsg.add(buf, len);
  harness->fuzz_request(httpmsg);
  harness->fuzz_response(httpmsg);
}

} // namespace
} // namespace Http1
} // namespace Http
} // namespace Envoy
