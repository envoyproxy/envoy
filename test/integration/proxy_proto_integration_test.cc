#include "test/integration/proxy_proto_integration_test.h"

#include "common/buffer/buffer_impl.h"

#include "test/test_common/printers.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST_F(ProxyProtoIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));

  Buffer::OwnedImpl buf("PROXY TCP4 1.2.3.4 255.255.255.255 65535 1234\r\n");
  conn->write(buf);

  testRouterRequestAndResponseWithBody(std::move(conn), Http::CodecClient::Type::HTTP1, 1024, 512,
                                       false);
}
} // Envoy
