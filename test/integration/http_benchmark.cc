#include <string>

#include "exe/process_wide.h"

#include "test/integration/http_integration.h"
#include "test/mocks/access_log/mocks.h"

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

namespace Envoy {

namespace {

// Get a debug string from an object by calling operator<<
template <typename T> std::string toString(const T& obj) {
  std::stringstream ss;
  ss << obj;
  return ss.str();
}

std::string versionString(Http::CodecClient::Type type) {
  switch (type) {
  case Http::CodecClient::Type::HTTP1:
    return "h1";
  case Http::CodecClient::Type::HTTP2:
    return "h2";
  case Http::CodecClient::Type::HTTP3:
    return "h3";
  }
}

std::string versionString(FakeHttpConnection::Type type) {
  switch (type) {
  case FakeHttpConnection::Type::HTTP1:
    return "h1";
  case FakeHttpConnection::Type::HTTP2:
    return "h2";
  }
}

} // namespace

class HttpBenchmarkImpl : public HttpIntegrationTest {
public:
  static Network::Address::IpVersion getSupportedIpVersion() {
    return TestEnvironment::getIpVersionsForTest().front();
  }

  HttpBenchmarkImpl(Http::CodecClient::Type downstream_protocol)
      : HttpIntegrationTest(downstream_protocol, getSupportedIpVersion()) {}

  void initialize() override {
    if (!Envoy::Event::Libevent::Global::initialized()) {
      Envoy::Event::Libevent::Global::initialize();
    }
    HttpIntegrationTest::initialize();
  }

  void doBenchmark(benchmark::State& state, uint64_t request_body_size, uint64_t response_body_size,
                   bool set_content_length) {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "POST"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    if (set_content_length) {
      request_headers.setContentLength(request_body_size);
      response_headers.setContentLength(response_body_size);
    }

    autonomous_upstream_ = false;
    initialize();

    for (auto _ : state) {
      codec_client_ = makeHttpConnection(lookupPort("http"));
      RELEASE_ASSERT(codec_client_->connected(),
                     std::string(codec_client_->connection()->transportFailureReason()));

      auto response_decoder = sendRequestAndWaitForResponse(
          request_headers, request_body_size, default_response_headers_, response_body_size);
      RELEASE_ASSERT(response_decoder->complete(), "Expected complete response");
      RELEASE_ASSERT(response_decoder->headers().Status()->value().getStringView() == "200",
                     toString(response_decoder->headers()));
      codec_client_->close();
      codec_client_.reset();
    }
  }

  Envoy::Thread::MutexBasicLockable lock_;
  Envoy::Logger::Context logging_state_{Envoy::TestEnvironment::getOptions().logLevel(),
                                        Envoy::TestEnvironment::getOptions().logFormat(), lock_,
                                        false};
};

void BM_http(benchmark::State& state) {
  Http::CodecClient::Type client_type = static_cast<Http::CodecClient::Type>(state.range(0));
  FakeHttpConnection::Type upstream_type = static_cast<FakeHttpConnection::Type>(state.range(1));
  const uint64_t request_body_size = state.range(2);
  const uint64_t response_body_size = state.range(3);
  const bool set_content_length = state.range(4);

  if (!Envoy::Event::Libevent::Global::initialized()) {
    Envoy::Event::Libevent::Global::initialize();
  }

  state.SetLabel(absl::StrCat("request: ", versionString(client_type), " ", request_body_size,
                              ", response: ", versionString(upstream_type), " ", response_body_size,
                              ", ", set_content_length ? "content-length" : "chunked"));
  HttpBenchmarkImpl http_benchmark(client_type);
  http_benchmark.setUpstreamProtocol(upstream_type);
  http_benchmark.doBenchmark(state, request_body_size, response_body_size, set_content_length);
}

static void BenchmarkArguments(benchmark::internal::Benchmark* b) {
  for (bool large_post : {false, true}) {
    for (Http::CodecClient::Type client_type :
         {Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2}) {
      for (FakeHttpConnection::Type upstream_type :
           {FakeHttpConnection::Type::HTTP1, FakeHttpConnection::Type::HTTP2}) {
        for (bool set_content_length : {false, true}) {
          int64_t request_body_size = large_post ? 10 * 1024 * 1024 : 0;
          int64_t response_body_size = large_post ? 0 : 10 * 1024 * 1024;
          b->Args({static_cast<int64_t>(client_type), static_cast<int64_t>(upstream_type),
                   request_body_size, response_body_size, set_content_length});
        }
      }
    }
  }
}

BENCHMARK(BM_http)
    ->Apply(BenchmarkArguments)
    ->MeasureProcessCPUTime()
    ->Unit(benchmark::kMillisecond);

} // namespace Envoy
