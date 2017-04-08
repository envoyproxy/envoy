#include "test/integration/proxy_proto_integration_test.h"

#include "common/buffer/buffer_impl.h"

TEST_F(ProxyProtoIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));

  Buffer::OwnedImpl buf("PROXY TCP4 1.2.3.4 255.255.255.255 66776 1234\r\n");
  conn->write(buf);

  testRouterRequestAndResponseWithBody(std::move(conn), Http::CodecClient::Type::HTTP1, 1024, 512,
                                       false);
}
