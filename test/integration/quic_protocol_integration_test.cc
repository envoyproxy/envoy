#include "test/integration/protocol_integration_test.h"

namespace Envoy {

// These will run with HTTP/3 downstream, and Http upstream.
INSTANTIATE_TEST_SUITE_P(DownstreamProtocols, DownstreamProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP3}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// These will run with HTTP/3 downstream, and Http and HTTP/2 upstream.
INSTANTIATE_TEST_SUITE_P(DownstreamProtocols, ProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// These will run with HTTP/1 and HTTP/2 downstream, and HTTP/3 upstream.
INSTANTIATE_TEST_SUITE_P(UpstreamProtocols, DownstreamProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2},
                             {Http::CodecType::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

INSTANTIATE_TEST_SUITE_P(UpstreamProtocols, ProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2},
                             {Http::CodecType::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace Envoy
