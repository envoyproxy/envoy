#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "envoy/http/codec.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/overload_manager.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Http {
namespace Http1 {
namespace {

class BenchRequestDecoder : public RequestDecoder {
public:
  class Handle : public RequestDecoderHandle {
  public:
    explicit Handle(BenchRequestDecoder& decoder) : decoder_(decoder) {}
    OptRef<RequestDecoder> get() override { return decoder_; }

  private:
    BenchRequestDecoder& decoder_;
  };

  // StreamDecoder
  void decodeData(Buffer::Instance&, bool) override {}
  void decodeMetadata(MetadataMapPtr&&) override {}

  // RequestDecoder
  void decodeHeaders(RequestHeaderMapSharedPtr&& headers, bool) override {
    headers_ = std::move(headers);
  }
  void decodeTrailers(RequestTrailerMapPtr&&) override {}
  void sendLocalReply(Code, absl::string_view, const std::function<void(ResponseHeaderMap&)>&,
                      const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) override {
    local_replies_++;
  }
  StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }
  AccessLog::InstanceSharedPtrVector accessLogHandlers() override { return {}; }
  RequestDecoderHandlePtr getRequestDecoderHandle() override {
    return std::make_unique<Handle>(*this);
  }

  const RequestHeaderMap* headers() const { return headers_.get(); }
  uint64_t localReplies() const { return local_replies_; }

private:
  RequestHeaderMapSharedPtr headers_;
  uint64_t local_replies_{};
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

class BenchServerCallbacks : public ServerConnectionCallbacks {
public:
  explicit BenchServerCallbacks(RequestDecoder& decoder) : decoder_(decoder) {}

  void onGoAway(GoAwayErrorCode) override {}
  RequestDecoder& newStream(ResponseEncoder& response_encoder, bool) override {
    response_encoder_ = &response_encoder;
    return decoder_;
  }

  ResponseEncoder* responseEncoder() const { return response_encoder_; }

private:
  RequestDecoder& decoder_;
  ResponseEncoder* response_encoder_{};
};

// A browser-shaped request whose cookie is the only header that varies in size.
std::string makeRequest(const int64_t cookie_bytes) {
  std::string request =
      "GET /api/v3/listings/1234567890?locale=en&currency=USD HTTP/1.1\r\n"
      "host: www.example.com\r\n"
      "user-agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, "
      "like Gecko) Chrome/124.0.0.0 Safari/537.36\r\n"
      "accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;"
      "q=0.8\r\n"
      "accept-language: en-US,en;q=0.9\r\n"
      "accept-encoding: gzip, deflate, br, zstd\r\n"
      "referer: https://www.example.com/s/San-Francisco--CA/homes?adults=2&checkin=2026-12-01\r\n"
      "x-forwarded-for: 203.0.113.17, 198.51.100.4\r\n"
      "x-request-id: 3f1c9b2e-7a4d-4e8f-9c21-5b6a0d8e7f13\r\n"
      "traceparent: 00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\r\n";
  if (cookie_bytes > 0) {
    std::string cookie;
    cookie.reserve(cookie_bytes + 64);
    for (int i = 0; static_cast<int64_t>(cookie.size()) < cookie_bytes; ++i) {
      absl::StrAppend(&cookie, i == 0 ? "" : "; ", "_session_", i,
                      "=a1B2c3D4e5F6g7H8i9J0-k_L%2Fm3N4o5P6q7R8s9T0");
    }
    cookie.resize(cookie_bytes);
    absl::StrAppend(&request, "cookie: ", cookie, "\r\n");
  }
  request.append("\r\n");
  return request;
}

void dispatchRequests(benchmark::State& state, const std::string& request) {
  testing::NiceMock<Network::MockConnection> connection;
  testing::NiceMock<Server::MockOverloadManager> overload_manager;
  Stats::IsolatedStoreImpl store;
  CodecStats::AtomicPtr stats_ptr;
  CodecStats& stats = CodecStats::atomicGet(stats_ptr, *store.rootScope());
  Http1Settings settings;
  BenchRequestDecoder decoder;
  BenchServerCallbacks callbacks(decoder);
  ServerConnectionImpl server_codec(connection, stats, callbacks, settings,
                                    DEFAULT_MAX_REQUEST_HEADERS_KB, DEFAULT_MAX_HEADERS_COUNT,
                                    envoy::config::core::v3::HttpProtocolOptions::ALLOW,
                                    overload_manager);
  ServerConnection& codec = server_codec;
  const TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  for (auto _ : state) {
    Buffer::OwnedImpl buffer(request);
    const Status status = codec.dispatch(buffer);
    if (!status.ok() || decoder.headers() == nullptr || decoder.localReplies() != 0) {
      state.SkipWithError("benchmark request failed to decode");
      return;
    }
    benchmark::DoNotOptimize(decoder.headers()->size());
    callbacks.responseEncoder()->encodeHeaders(response_headers, true);
    connection.dispatcher_.to_delete_.clear();
  }
  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(request.size()));
}

// Decodes a request carrying realistic browser headers and a cookie of the given size.
static void bmServerDispatchRequest(benchmark::State& state) {
  dispatchRequests(state, makeRequest(state.range(0)));
}
BENCHMARK(bmServerDispatchRequest)->Arg(0)->Arg(1024)->Arg(3072)->ArgName("cookie_bytes");

// A request with no header lines, so no header value reaches the codec.
static void bmServerDispatchRequestNoHeaders(benchmark::State& state) {
  dispatchRequests(state, "GET / HTTP/1.1\r\n\r\n");
}
BENCHMARK(bmServerDispatchRequestNoHeaders);

} // namespace
} // namespace Http1
} // namespace Http
} // namespace Envoy
