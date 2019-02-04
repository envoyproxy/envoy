#pragma once

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_base.h"

using testing::ReturnRef;

namespace Envoy {
class SslCertsTest : public TestBase {
public:
  static void SetUpTestSuite() {
    TestEnvironment::exec({TestEnvironment::runfilesPath(
        "test/extensions/transport_sockets/tls/gen_unittest_certs.sh")});
  }

protected:
  SslCertsTest() : api_(Api::createApiForTest(store_)) {
    ON_CALL(factory_context_, api()).WillByDefault(ReturnRef(*api_));
  }

  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  Stats::IsolatedStoreImpl store_;
  Api::ApiPtr api_;
};
} // namespace Envoy
