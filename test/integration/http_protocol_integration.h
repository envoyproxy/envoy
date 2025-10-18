#pragma once

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

struct HttpProtocolTestParams {
  Network::Address::IpVersion version;
  Http::CodecType downstream_protocol;
  Http::CodecType upstream_protocol;
  Http2Impl http2_implementation;
  bool use_universal_header_validator;
};

absl::string_view http2ImplementationToString(Http2Impl impl);

// Allows easy testing of Envoy code for HTTP/HTTP2 upstream/downstream.
//
// Usage:
//
// using MyTest = HttpProtocolIntegrationTest;
//
// INSTANTIATE_TEST_SUITE_P(Protocols, MyTest,
//                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
//                         HttpProtocolIntegrationTest::protocolTestParamsToString);
//
//
// TEST_P(MyTest, TestInstance) {
// ....
// }
// TODO(#20996) consider switching to SimulatedTimeSystem instead of using real time.
class HttpProtocolIntegrationTestBase : public HttpIntegrationTest, public testing::Test {
public:
  // By default returns 8 combinations of
  // [HTTP  upstream / HTTP  downstream] x [Ipv4, IPv6]
  // [HTTP  upstream / HTTP2 downstream] x [IPv4, Ipv6]
  // [HTTP2 upstream / HTTP  downstream] x [Ipv4, IPv6]
  // [HTTP2 upstream / HTTP2 downstream] x [IPv4, Ipv6]
  //
  // Upstream and downstream protocols may be changed via the input vectors.
  // Address combinations are propagated from TestEnvironment::getIpVersionsForTest()
  static std::vector<HttpProtocolTestParams>
  getProtocolTestParams(const std::vector<Http::CodecType>& downstream_protocols =
                            {
                                Http::CodecType::HTTP1,
                                Http::CodecType::HTTP2,
                                Http::CodecType::HTTP3,
                            },
                        const std::vector<Http::CodecType>& upstream_protocols = {
                            Http::CodecType::HTTP1,
                            Http::CodecType::HTTP2,
                            Http::CodecType::HTTP3,
                        });

  static std::vector<HttpProtocolTestParams> getProtocolTestParamsWithoutHTTP3() {
    return getProtocolTestParams(
        /*downstream_protocols = */ {Http::CodecType::HTTP1, Http::CodecType::HTTP2},
        /*upstream_protocols = */ {Http::CodecType::HTTP1, Http::CodecType::HTTP2});
  }

  // Allows pretty printed test names of the form
  // FooTestCase.BarInstance/IPv4_Http2Downstream_HttpUpstream
static std::string testNameFromTestParams(
    const HttpProtocolTestParams& params);
  static std::string
  protocolTestParamsToString(const ::testing::TestParamInfo<HttpProtocolTestParams>& p);

  HttpProtocolIntegrationTestBase(const HttpProtocolTestParams& test_params)
      : HttpProtocolIntegrationTestBase(ConfigHelper::httpProxyConfig(
            /*downstream_is_quic=*/test_params.downstream_protocol == Http::CodecType::HTTP3), test_params) {}

  HttpProtocolIntegrationTestBase(const std::string config, const HttpProtocolTestParams& test_params)
      : HttpIntegrationTest(test_params.downstream_protocol, test_params.version, config), test_params_(test_params),
        use_universal_header_validator_(test_params.use_universal_header_validator) {
    setupHttp2ImplOverrides(test_params.http2_implementation);
    config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                      test_params.use_universal_header_validator ? "true" : "false");
  }

  void SetUp() override {
    setDownstreamProtocol(test_params_.downstream_protocol);
    setUpstreamProtocol(test_params_.upstream_protocol);
  }

  void initialize() override {
    if (async_lb_) {
      config_helper_.setAsyncLb();
    }
    HttpIntegrationTest::initialize();
  }

  void setDownstreamOverrideStreamErrorOnInvalidHttpMessage();
  void setUpstreamOverrideStreamErrorOnInvalidHttpMessage();

protected:
  // Current test params.
  const HttpProtocolTestParams& test_params_;
  const bool use_universal_header_validator_{false};
  bool async_lb_ = true;
};


// Variadic template used to add additional test params to Http protocol integration tests.
template <typename... T>
class HttpProtocolIntegrationTestWithParams : 
                                public testing::WithParamInterface<typename std::conditional<(sizeof...(T) > 0), std::tuple<HttpProtocolTestParams, T...>, HttpProtocolTestParams>::type>,
                                public HttpProtocolIntegrationTestBase {
static const HttpProtocolTestParams&
getTestBaseParam() {
    if constexpr (sizeof...(T) > 0) {
        return std::get<0>(testing::TestWithParam<std::tuple<HttpProtocolTestParams, T...>>::GetParam());
    } else {
        return testing::TestWithParam<HttpProtocolTestParams>::GetParam();
    }
}
public:
    HttpProtocolIntegrationTestWithParams() : HttpProtocolIntegrationTestBase(getTestBaseParam()) {}
};

// Basic class used for testing without additional parameters.
using HttpProtocolIntegrationTest = HttpProtocolIntegrationTestWithParams<>;

class UpstreamDownstreamIntegrationTest
    : public testing::TestWithParam<std::tuple<HttpProtocolTestParams, bool>>,
      public HttpIntegrationTest {
public:
  UpstreamDownstreamIntegrationTest()
      : HttpIntegrationTest(
            std::get<0>(GetParam()).downstream_protocol, std::get<0>(GetParam()).version,
            ConfigHelper::httpProxyConfig(std::get<0>(GetParam()).downstream_protocol ==
                                          Http::CodecType::HTTP3)) {
    setupHttp2ImplOverrides(std::get<0>(GetParam()).http2_implementation);
    config_helper_.addRuntimeOverride(
        "envoy.reloadable_features.enable_universal_header_validator",
        std::get<0>(GetParam()).use_universal_header_validator ? "true" : "false");
  }
  static std::string testParamsToString(
      const ::testing::TestParamInfo<std::tuple<HttpProtocolTestParams, bool>>& params) {
    return fmt::format(
        "{}_{}",
        HttpProtocolIntegrationTest::protocolTestParamsToString(
            ::testing::TestParamInfo<HttpProtocolTestParams>(std::get<0>(params.param), 0)),
        std::get<1>(params.param) ? "DownstreamFilter" : "UpstreamFilter");
  }

  static std::vector<std::tuple<HttpProtocolTestParams, bool>>
  getDefaultTestParams(const std::vector<Http::CodecType>& downstream_protocols =
                           {
                               Http::CodecType::HTTP1,
                               Http::CodecType::HTTP2,
                               Http::CodecType::HTTP3,
                           },
                       const std::vector<Http::CodecType>& upstream_protocols = {
                           Http::CodecType::HTTP1,
                           Http::CodecType::HTTP2,
                           Http::CodecType::HTTP3,
                       }) {
    std::vector<std::tuple<HttpProtocolTestParams, bool>> ret;
    std::vector<HttpProtocolTestParams> protocol_defaults =
        HttpProtocolIntegrationTest::getProtocolTestParams(downstream_protocols,
                                                           upstream_protocols);
    const std::vector<bool> testing_downstream_filter_values{true, false};

    for (auto& param : protocol_defaults) {
      for (bool testing_downstream_filter : testing_downstream_filter_values) {
        ret.push_back(std::make_tuple(param, testing_downstream_filter));
      }
    }
    return ret;
  }

  void SetUp() override {
    setDownstreamProtocol(std::get<0>(GetParam()).downstream_protocol);
    setUpstreamProtocol(std::get<0>(GetParam()).upstream_protocol);
    testing_downstream_filter_ = std::get<1>(GetParam());
  }

  void initialize() override {
    if (async_lb_) {
      config_helper_.setAsyncLb();
    }
    HttpIntegrationTest::initialize();
  }

  bool testing_downstream_filter_;
  bool async_lb_ = true;
};

} // namespace Envoy
