#include "test/integration/proxy_proto_integration_test.h"

#include "common/buffer/buffer_impl.h"

#include "test/test_common/printers.h"

#include "gtest/gtest.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(IpVersions, ProxyProtoIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(ProxyProtoIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));

  Buffer::OwnedImpl buf("PROXY TCP4 1.2.3.4 254.254.254.254 65535 1234\r\n");
  conn->write(buf);

  testRouterRequestAndResponseWithBody(std::move(conn), Http::CodecClient::Type::HTTP1, 1024, 512,
                                       false);
}

TEST_P(ProxyProtoIntegrationTest, RouterRequestAndResponseWithBodyNoBufferV6) {
  Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));

  Buffer::OwnedImpl buf("PROXY TCP6 1:2:3::4 FE00:: 65535 1234\r\n");
  conn->write(buf);

  testRouterRequestAndResponseWithBody(std::move(conn), Http::CodecClient::Type::HTTP1, 1024, 512,
                                       false);
}

} // namespace Envoy
