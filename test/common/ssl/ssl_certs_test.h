#pragma once

#include "test/test_common/environment.h"
#include "test/mocks/secret/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
class SslCertsTest : public testing::Test {
public:
  static void SetUpTestCase() {
    TestEnvironment::exec({TestEnvironment::runfilesPath("test/common/ssl/gen_unittest_certs.sh")});
  }

  Secret::MockSecretManager secret_manager_;
};
} // namespace Envoy
