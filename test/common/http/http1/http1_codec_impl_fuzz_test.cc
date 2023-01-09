#include "source/common/http/conn_manager_utility.h"
#include "source/common/http/exception.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/http1/codec_impl.h"

#include "test/common/http/http1/http1_codec_impl_fuzz.pb.h"
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

// KISS fuzz
//  1) RequestDecoder for untrusted downstreams
//  2) ResponseDecoder for untrusted upstreams.
// In HTTP1, requests and responses are quite similar. Let the fuzzer generate
// one set of headers and body to be used with both and some artifacts for the
// HTTP Request and HTTP Response lines. To improve performance, requests and
// responses are encoded s.t. they resemble valid HTTP streams. This
// is a trade-off and subject to opinion. E.g. coverage could be gained by
// relaxing the constraints on the output of the mutator at the cost of
// performance.

using FuzzCase = test::common::http::http1::Http1CodecImplFuzzTestCase;
static void convertFuzzCase(const FuzzCase& input, Buffer::Instance& request,
                            Buffer::Instance& response) {
  // Intentionally do not *validate* input in any way!
  auto add_headers = [&input](Buffer::Instance& buf) {
    if (input.has_headers()) {
      auto hdrs = input.headers();
      for (int i = 0; i < hdrs.headers_size(); i++) {
        auto hdr = hdrs.headers(i);
        std::string header = absl::Substitute("$0: $1\r\n", hdr.key(), hdr.value());
        buf.add(header);
      }
    }
  };
  auto add_body = [&input](Buffer::Instance& buf) {
    absl::string_view clrf = "\r\n";
    buf.add(clrf);
    buf.add(input.body());
  };

  if (input.has_req()) {
    auto req = input.req();
    std::string request_line = absl::Substitute("$0 $1 HTTP/1.1\r\n", req.method(), req.path());
    request.add(request_line);
    add_headers(request);
    add_body(request);
  }

  if (input.has_resp()) {
    auto resp = input.resp();
    std::string response_line = absl::Substitute("$0 $1", resp.status(), resp.msg());
    response.add(response_line);
    add_headers(response);
    add_body(response);
  }
}

DEFINE_PROTO_FUZZER(const FuzzCase& input) {
  if (harness == nullptr) {
    Http1Settings server_settings = fromHttp1Settings();
    Http1Settings client_settings = fromHttp1Settings();
    harness = std::make_unique<Http1Harness>(server_settings, client_settings);
    atexit(reset_harness);
  }

  Buffer::OwnedImpl request, response;
  convertFuzzCase(input, request, response);
  harness->fuzz_request(request);
  harness->fuzz_response(response);
}

} // namespace
} // namespace Http1
} // namespace Http
} // namespace Envoy
