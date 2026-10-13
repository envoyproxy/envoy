#pragma once

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

// A test class for testing HTTP/1.1 upstream and downstreams
namespace Envoy {
// TODO(#28841) parameterize to run with and without UHV
class IntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                        public HttpIntegrationTest {
public:
  IntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}
};

// TODO(#28841) parameterize to run with and without UHV
class UpstreamEndpointIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public HttpIntegrationTest {
public:
  UpstreamEndpointIntegrationTest()
      : HttpIntegrationTest(
            Http::CodecType::HTTP1,
            [](int) {
              return Network::Utility::parseInternetAddressNoThrow(
                  Network::Test::getLoopbackAddressString(GetParam()), 0);
            },
            GetParam()) {}
};
} // namespace Envoy
