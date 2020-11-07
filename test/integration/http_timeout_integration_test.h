#pragma once

#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
class HttpTimeoutIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public Event::TestUsingSimulatedTime,
                                   public HttpIntegrationTest {
public:
  // Arbitrarily choose HTTP2 here, the tests for this class are around
  // timeouts which don't have version specific behavior.
  HttpTimeoutIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void SetUp() override {
    setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  }

  void testRouterRequestAndResponseWithHedgedPerTryTimeout(uint64_t request_size,
                                                           uint64_t response_size,
                                                           bool first_request_wins);

  void initialize() override {
    if (respect_expected_rq_timeout) {
      config_helper_.addConfigModifier(
          [&](envoy::extensions::filters::network::http_connection_manager::v3::
                  HttpConnectionManager& hcm) {
            envoy::extensions::filters::http::router::v3::Router router_config;
            router_config.set_respect_expected_rq_timeout(respect_expected_rq_timeout);
            // TestUtility::jsonConvert(router_config,
            // *hcm.mutable_http_filters(0)->mutable_config());
            hcm.mutable_http_filters(0)->mutable_typed_config()->PackFrom(router_config);
          });
    }

    HttpIntegrationTest::initialize();
  }

  void enableRespectExpectedRqTimeout(bool enable) { respect_expected_rq_timeout = enable; }

  bool respect_expected_rq_timeout{false};
};

} // namespace Envoy
