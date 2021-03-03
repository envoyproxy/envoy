#include "test/integration/protocol_integration_test.h"

namespace Envoy {

// We do not yet run QUIC downstream tests.
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(DownstreamProtocolIntegrationTest);

// This will run with HTTP/1 and HTTP/2 downstream, and HTTP/3 upstream.
INSTANTIATE_TEST_SUITE_P(Protocols, ProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2},
                             {FakeHttpConnection::Type::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace Envoy
