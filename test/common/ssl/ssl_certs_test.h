#pragma once

#include "test/test_common/environment.h"

class SslCertsTest : public testing::Test {
public:
  static void SetUpTestCase() {
    TestEnvironment::exec({TestEnvironment::runfilesPath("test/common/ssl/gen_unittest_certs.sh")});
  }
};
