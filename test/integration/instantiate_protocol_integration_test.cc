#include "test/integration/protocol_integration_test.h"

namespace Envoy {

// For tests which focus on downstream-to-Envoy behavior, and don't need to be
// run with both HTTP/1 and HTTP/2 upstreams.
INSTANTIATE_TEST_SUITE_P(Protocols, DownstreamProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

INSTANTIATE_TEST_SUITE_P(
    Protocols, ProtocolIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace Envoy
