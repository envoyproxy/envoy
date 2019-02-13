#pragma once

#include "test/integration/http_integration.h"
#include "test/test_common/test_base.h"

// A test class for testing HTTP/1.1 upstream and downstreams
namespace Envoy {
class IntegrationTest : public TestBaseWithParam<Network::Address::IpVersion>,
                        public HttpIntegrationTest {
public:
  IntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam(), realTime()) {}
};
} // namespace Envoy
