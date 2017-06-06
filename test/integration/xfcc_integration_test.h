#pragma once

#include <memory>
#include <string>

#include "test/integration/integration.h"
#include "test/integration/server.h"
#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::NiceMock;

namespace Xfcc {

class XfccIntegrationTest : public BaseIntegrationTest, public testing::Test {
public:
  const std::string xfcc_header_ = "BY=test://bar.com/client;Hash=123456;SAN=test://foo.com/frontend";

  void SetUp() override;
  void TearDown() override;

  Network::ClientConnectionPtr makeSslClientConnection();
  Ssl::ServerContextPtr createUpstreamSslContext();
  Ssl::ClientContextPtr createClientSslContext();
  void testRequestAndResponseWithXfccHeader(
      Network::ClientConnectionPtr&& conn, std::string expected_xfcc);

private:
  std::unique_ptr<Runtime::Loader> runtime_;
  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Ssl::ServerContextPtr upstream_ssl_ctx_;

  std::string replaceXfccConfigs(std::string config, std::string content);
};
} // Xfcc
} // Envoy
