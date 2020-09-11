#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "library/common/api/external.h"
#include "library/common/extensions/filters/http/platform_bridge/filter.h"
#include "library/common/extensions/filters/http/platform_bridge/filter.pb.h"

using testing::ByMove;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PlatformBridge {
namespace {

class PlatformBridgeFilterTest : public testing::Test {
public:
  void setUpFilter(std::string&& yaml, envoy_http_filter* platform_filter) {
    envoymobile::extensions::filters::http::platform_bridge::PlatformBridge config;
    TestUtility::loadFromYaml(yaml, config);
    Api::External::registerApi(config.platform_filter_name(), platform_filter);

    config_ = std::make_shared<PlatformBridgeFilterConfig>(config);
    filter_ = std::make_unique<PlatformBridgeFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  typedef struct {
    unsigned int init_filter_calls;
    unsigned int on_request_headers_calls;
    unsigned int on_request_data_calls;
    unsigned int on_request_trailers_calls;
    unsigned int on_response_headers_calls;
    unsigned int on_response_data_calls;
    unsigned int on_response_trailers_calls;
    unsigned int release_filter_calls;
  } filter_invocations;

  PlatformBridgeFilterConfigSharedPtr config_{};
  std::unique_ptr<PlatformBridgeFilter> filter_{};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(PlatformBridgeFilterTest, BasicContinueOnRequestHeaders) {
  envoy_http_filter platform_filter;
  filter_invocations invocations = {0, 0, 0, 0, 0, 0, 0, 0};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(c_headers.headers[0].key.bytes),
                          c_headers.headers[0].key.length),
              ":authority");
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(c_headers.headers[0].value.bytes),
                          c_headers.headers[0].value.length),
              "test.code");
    EXPECT_TRUE(end_stream);
    invocations->on_request_headers_calls++;
    return {kEnvoyFilterHeadersStatusContinue, c_headers};
  };

  setUpFilter(R"EOF(
platform_filter_name: BasicContinueOnRequestHeaders
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(invocations.on_request_headers_calls, 1);
}

} // namespace
} // namespace PlatformBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
