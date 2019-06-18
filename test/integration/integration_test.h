#pragma once

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

// A test class for testing HTTP/1.1 upstream and downstreams
namespace Envoy {
class IntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                        public HttpIntegrationTest {
public:
  IntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}
};

class UpstreamEndpointIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public HttpIntegrationTest {
public:
  UpstreamEndpointIntegrationTest()
      : HttpIntegrationTest(
            Http::CodecClient::Type::HTTP1,
            [](int) {
              return Network::Utility::parseInternetAddress(
                  Network::Test::getLoopbackAddressString(GetParam()), 0);
            },
            GetParam()) {}
};
} // namespace Envoy
