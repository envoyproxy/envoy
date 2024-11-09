#include "source/common/http/header_map_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/common/expr/context.h"

#include "test/mocks/local_info/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

namespace Envoy {

using testing::NiceMock;
using testing::Return;

class ExpressionContextSpeedTest {
public:
  ExpressionContextSpeedTest(size_t num_attributes) {
    ssl_info_ = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();

    // Set up request headers
    request_headers_ = makeRequestHeaders();

    // Set up response data
    response_headers_ = makeResponseHeaders();
    response_trailers_ = makeResponseTrailers();

    // Set up stream info
    setupStreamInfo();

    // Set up filter state with specified number of attributes
    setupFilterState(num_attributes);

    // Create all wrappers
    request_ = std::make_unique<Extensions::Filters::Common::Expr::RequestWrapper>(
        arena_, &request_headers_, info_);
    response_ = std::make_unique<Extensions::Filters::Common::Expr::ResponseWrapper>(
        arena_, &response_headers_, &response_trailers_, info_);
    connection_ =
        std::make_unique<Extensions::Filters::Common::Expr::ConnectionWrapper>(arena_, info_);
    filter_state_ = std::make_unique<Extensions::Filters::Common::Expr::FilterStateWrapper>(
        arena_, *info_.filterState());
    xds_ = std::make_unique<Extensions::Filters::Common::Expr::XDSWrapper>(arena_, &info_,
                                                                           &local_info_);
  }

  void testRequestAttributes(::benchmark::State& state) {
    static const std::vector<absl::string_view> attributes = {
        "scheme", "path", "method", "referer", "user-agent", "size", "total_size", "protocol"};
    size_t idx = 0;

    for (auto _ : state) { // NOLINT
      auto attr = attributes[idx];
      auto value = (*request_)[Extensions::Filters::Common::Expr::CelValue::CreateStringView(attr)];
      benchmark::DoNotOptimize(value);
      idx = (idx + 1) % attributes.size();
    }
  }

  void testResponseAttributes(::benchmark::State& state) {
    static const std::vector<absl::string_view> attributes = {"code", "size", "headers", "trailers",
                                                              "grpc_status"};
    size_t idx = 0;

    for (auto _ : state) { // NOLINT
      auto attr = attributes[idx];
      auto value =
          (*response_)[Extensions::Filters::Common::Expr::CelValue::CreateStringView(attr)];
      benchmark::DoNotOptimize(value);
      idx = (idx + 1) % attributes.size();
    }
  }

  void testConnectionAttributes(::benchmark::State& state) {
    static const std::vector<absl::string_view> attributes = {"mtls", "dns_san_peer_certificate"};
    size_t idx = 0;

    for (auto _ : state) { // NOLINT
      auto attr = attributes[idx];
      auto value =
          (*connection_)[Extensions::Filters::Common::Expr::CelValue::CreateStringView(attr)];
      benchmark::DoNotOptimize(value);
      idx = (idx + 1) % attributes.size();
    }
  }

  void testFilterState(::benchmark::State& state) {
    for (auto _ : state) { // NOLINT
      for (const auto& key : filter_state_keys_) {
        auto value =
            (*filter_state_)[Extensions::Filters::Common::Expr::CelValue::CreateStringView(key)];
        benchmark::DoNotOptimize(value);
      }
    }
  }

private:
  Http::TestRequestHeaderMapImpl makeRequestHeaders() {
    return Http::TestRequestHeaderMapImpl{{":method", "POST"},      {":scheme", "http"},
                                          {":path", "/meow?yes=1"}, {":authority", "kittens.com"},
                                          {"referer", "dogs.com"},  {"user-agent", "envoy-mobile"},
                                          {"content-length", "10"}, {"x-request-id", "blah"}};
  }

  Http::TestResponseHeaderMapImpl makeResponseHeaders() {
    return Http::TestResponseHeaderMapImpl{{"test-header", "value"}, {"grpc-status", "0"}};
  }

  Http::TestResponseTrailerMapImpl makeResponseTrailers() {
    return Http::TestResponseTrailerMapImpl{{"test-trailer", "trailer-value"}};
  }

  void setupStreamInfo() {
    EXPECT_CALL(info_, bytesReceived()).WillRepeatedly(Return(10));
    EXPECT_CALL(info_, bytesSent()).WillRepeatedly(Return(100));
    EXPECT_CALL(info_, responseCode()).WillRepeatedly(Return(200));
    EXPECT_CALL(info_, protocol()).WillRepeatedly(Return(Http::Protocol::Http2));

    const SystemTime start_time(std::chrono::milliseconds(1522796769123));
    EXPECT_CALL(info_, startTime()).WillRepeatedly(Return(start_time));

    info_.downstream_connection_info_provider_->setSslConnection(ssl_info_);

    auto local = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
    auto remote = Network::Utility::parseInternetAddressNoThrow("10.20.30.40", 456, false);
    info_.downstream_connection_info_provider_->setLocalAddress(local);
    info_.downstream_connection_info_provider_->setRemoteAddress(remote);
  }

  void setupFilterState(size_t num_attributes) {
    for (size_t i = 0; i < num_attributes; i++) {
      std::string key = absl::StrCat("key_", i);
      std::string value = absl::StrCat("value_", i);
      auto accessor = std::make_shared<Router::StringAccessorImpl>(value);
      info_.filterState()->setData(key, accessor, StreamInfo::FilterState::StateType::ReadOnly);
      filter_state_keys_.push_back(key);
    }
  }

  // Member variables
  Protobuf::Arena arena_;
  NiceMock<StreamInfo::MockStreamInfo> info_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  std::shared_ptr<NiceMock<Ssl::MockConnectionInfo>> ssl_info_;

  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  std::vector<std::string> filter_state_keys_;

  std::unique_ptr<Extensions::Filters::Common::Expr::RequestWrapper> request_;
  std::unique_ptr<Extensions::Filters::Common::Expr::ResponseWrapper> response_;
  std::unique_ptr<Extensions::Filters::Common::Expr::ConnectionWrapper> connection_;
  std::unique_ptr<Extensions::Filters::Common::Expr::FilterStateWrapper> filter_state_;
  std::unique_ptr<Extensions::Filters::Common::Expr::XDSWrapper> xds_;
};

// Individual benchmark functions
static void bmRequestAttributes(::benchmark::State& state) {
  ExpressionContextSpeedTest speed_test(state.range(0));
  speed_test.testRequestAttributes(state);
}

static void bmResponseAttributes(::benchmark::State& state) {
  ExpressionContextSpeedTest speed_test(state.range(0));
  speed_test.testResponseAttributes(state);
}

static void bmConnectionAttributes(::benchmark::State& state) {
  ExpressionContextSpeedTest speed_test(state.range(0));
  speed_test.testConnectionAttributes(state);
}

static void bmFilterState(::benchmark::State& state) {
  ExpressionContextSpeedTest speed_test(state.range(0));
  speed_test.testFilterState(state);
}

BENCHMARK(bmRequestAttributes)
    ->Unit(::benchmark::kMicrosecond)
    ->RangeMultiplier(100)
    ->Range(100, 1000000);

BENCHMARK(bmResponseAttributes)
    ->Unit(::benchmark::kMicrosecond)
    ->RangeMultiplier(100)
    ->Range(100, 1000000);

BENCHMARK(bmFilterState)
    ->Unit(::benchmark::kMicrosecond)
    ->RangeMultiplier(100)
    ->Range(1000, 10000000);

BENCHMARK(bmConnectionAttributes)
    ->Unit(::benchmark::kMicrosecond)
    ->RangeMultiplier(100)
    ->Range(100, 1000000);
} // namespace Envoy
