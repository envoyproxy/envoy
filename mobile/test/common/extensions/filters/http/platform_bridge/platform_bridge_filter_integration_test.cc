#include "source/common/http/header_map_impl.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "library/common/api/external.h"
#include "library/common/extensions/filters/http/platform_bridge/filter.h"

namespace Envoy {

class PlatformBridgeIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  PlatformBridgeIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  const std::string filter_config_1{R"EOF(
      name: envoy.filters.http.platform_bridge
      typed_config:
        "@type": type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge
        platform_filter_name: filter_1
    )EOF"};

  const std::string filter_config_2{R"EOF(
      name: envoy.filters.http.platform_bridge
      typed_config:
        "@type": type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge
        platform_filter_name: filter_2
    )EOF"};

  void customInit(envoy_http_filter* filter_1, envoy_http_filter* filter_2) {
    setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);

    Api::External::registerApi("filter_1", filter_1);
    Api::External::registerApi("filter_2", filter_2);

    config_helper_.addFilter(filter_config_1);
    config_helper_.addFilter(filter_config_2);

    HttpIntegrationTest::initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
  }

  void TearDown() override {
    HttpIntegrationTest::cleanupUpstreamAndDownstream();
    codec_client_->close();
    codec_client_.reset();
  }

  typedef struct {
    unsigned int init_filter_calls;
    unsigned int on_request_headers_calls;
    unsigned int on_request_data_calls;
    unsigned int on_request_trailers_calls;
    unsigned int on_response_headers_calls;
    unsigned int on_response_data_calls;
    unsigned int on_response_trailers_calls;
    unsigned int set_request_callbacks_calls;
    unsigned int on_resume_request_calls;
    unsigned int set_response_callbacks_calls;
    unsigned int on_resume_response_calls;
    unsigned int release_filter_calls;
  } filter_invocations;
};

// INSTANTIATE_TEST_SUITE_P(IpVersions, PlatformBridgeIntegrationTest,
//                          testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// TEST_P(PlatformBridgeIntegrationTest, MultipleFilters) {
//   envoy_http_filter platform_filter_1{};
//   filter_invocations invocations_1{};
//   platform_filter_1.static_context = &invocations_1;
//   platform_filter_1.init_filter = [](const void* context) -> const void* {
//     envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
//     filter_invocations* invocations =
//         static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
//     invocations->init_filter_calls++;
//     return invocations;
//   };
//   platform_filter_1.on_response_data = [](envoy_data c_data, bool,
//                                           const void* context) -> envoy_filter_data_status {
//     filter_invocations* invocations =
//     static_cast<filter_invocations*>(const_cast<void*>(context));
//     invocations->on_response_data_calls++;
//     return {kEnvoyFilterDataStatusContinue, c_data, nullptr};
//   };
//   platform_filter_1.release_filter = [](const void*) {};

//   envoy_http_filter platform_filter_2{};
//   filter_invocations invocations_2{};
//   platform_filter_2.static_context = &invocations_2;
//   platform_filter_2.init_filter = [](const void* context) -> const void* {
//     envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
//     filter_invocations* invocations =
//         static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
//     invocations->init_filter_calls++;
//     return invocations;
//   };
//   platform_filter_2.on_response_data = [](envoy_data c_data, bool end_stream,
//                                           const void* context) -> envoy_filter_data_status {
//     filter_invocations* invocations =
//     static_cast<filter_invocations*>(const_cast<void*>(context));
//     invocations->on_response_data_calls++;
//     if (!end_stream) {
//       c_data.release(c_data.context);
//       return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
//     } else {
//       return {kEnvoyFilterDataStatusResumeIteration, c_data, nullptr};
//     }
//   };
//   platform_filter_2.release_filter = [](const void*) {};

//   customInit(&platform_filter_1, &platform_filter_2);

//   auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

//   // Wait for frames to arrive upstream.
//   ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_,
//   fake_upstream_connection_));
//   ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
//   ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

//   upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

//   upstream_request_->encodeData(100, false);
//   upstream_request_->encodeData(100, true);

//   // Wait for frames to arrive downstream.
//   ASSERT_TRUE(response->waitForEndStream());

//   EXPECT_TRUE(response->complete());
//   EXPECT_EQ("200", response->headers().Status()->value().getStringView());
// }

} // namespace Envoy
