#pragma once

#include "test/integration/http_integration.h"
#include "test/test_common/test_base.h"

namespace Envoy {
class IntegrationTest : public HttpIntegrationTest,
                        public TestBaseWithParam<Network::Address::IpVersion> {
public:
  IntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam(), realTime()) {}
};
} // namespace Envoy
