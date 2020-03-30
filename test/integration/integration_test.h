#pragma once

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

// A test class for testing HTTP/1.1 upstream and downstreams
namespace Envoy {
class IntegrationTest : public testing::TestWithParam<
                            std::tuple<Network::Address::IpVersion, Http::CodecClient::Type>>,
                        public HttpIntegrationTest {
public:
  IntegrationTest() : HttpIntegrationTest(std::get<1>(GetParam()), std::get<0>(GetParam())) {}
};

class UpstreamEndpointIntegrationTest
    : public testing::TestWithParam<
          std::tuple<Network::Address::IpVersion, Http::CodecClient::Type>>,
      public HttpIntegrationTest {
public:
  UpstreamEndpointIntegrationTest()
      : HttpIntegrationTest(
            std::get<1>(GetParam()),
            [](int) {
              return Network::Utility::parseInternetAddress(
                  Network::Test::getLoopbackAddressString(std::get<0>(GetParam())), 0);
            },
            std::get<0>(GetParam())) {}
};
} // namespace Envoy
