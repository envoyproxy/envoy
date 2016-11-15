#include "uds_integration_test.h"

#include "common/event/dispatcher_impl.h"

TEST_F(UdsIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(IntegrationTest::HTTP_PORT),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, false);
}

TEST_F(UdsIntegrationTest, RouterHeaderOnlyRequestAndResponse) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(IntegrationTest::HTTP_PORT),
                                         Http::CodecClient::Type::HTTP1);
}

TEST_F(UdsIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP1);
}

TEST_F(UdsIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP1);
}

TEST_F(UdsIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP1);
}
