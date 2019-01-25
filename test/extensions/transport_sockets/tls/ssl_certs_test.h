#pragma once

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
class SslCertsTest : public testing::Test {
public:
  static void SetUpTestSuite() {
    TestEnvironment::exec({TestEnvironment::runfilesPath(
        "test/extensions/transport_sockets/tls/gen_unittest_certs.sh")});
  }

  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
};
} // namespace Envoy
