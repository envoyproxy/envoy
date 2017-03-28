#pragma once

#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/server.h"
#include "test/test_common/environment.h"

#include "common/http/codec_client.h"
#include "common/stats/stats_impl.h"

class UdsIntegrationTest : public BaseIntegrationTest, public testing::Test {
public:
  /**
   * Global initializer for all integration tests.
   */
  static void SetUpTestCase() {
    test_server_ = IntegrationTestServer::create("test/config/integration/server_uds.json");
    fake_upstreams_.emplace_back(new FakeUpstream(
        TestEnvironment::unixDomainSocketPath("udstest.1.sock"), FakeHttpConnection::Type::HTTP1));
    fake_upstreams_.emplace_back(new FakeUpstream(
        TestEnvironment::unixDomainSocketPath("udstest.2.sock"), FakeHttpConnection::Type::HTTP1));
  }

  /**
   * Global destructor for all integration tests.
   */
  static void TearDownTestCase() {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};
