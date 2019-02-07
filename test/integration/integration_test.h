#pragma once

#include "test/integration/http_integration.h"
#include "test/test_common/test_base.h"

// A test class for testing HTTP/1.1 upstream and downstreams
namespace Envoy {
class IntegrationTest : public HttpIntegrationTest,
                        public TestBaseWithParam<Network::Address::IpVersion> {
public:
  IntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam(), realTime()) {}
};

class UpstreamEndpointIntegrationTest
    : public HttpIntegrationTest, public testing::TestWithParam<Network::Address::IpVersion> {
public:
  UpstreamEndpointIntegrationTest()
      : HttpIntegrationTest(
            Http::CodecClient::Type::HTTP1,
            Network::Utility::parseInternetAddress(Network::Test::getAnyAddressString(GetParam())),
            /*upstream_port_fn=*/[]{return 0;}) {}
};
} // namespace Envoy
