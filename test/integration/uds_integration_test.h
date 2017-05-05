#pragma once

#include "common/http/codec_client.h"
#include "common/stats/stats_impl.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/server.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

class UdsIntegrationTest : public BaseIntegrationTest, public testing::Test {
public:
  /**
   * Global initializer for all integration tests.
   */
  void SetUp() override {
    fake_upstreams_.emplace_back(new FakeUpstream(
        TestEnvironment::unixDomainSocketPath("udstest.1.sock"), FakeHttpConnection::Type::HTTP1));
    fake_upstreams_.emplace_back(new FakeUpstream(
        TestEnvironment::unixDomainSocketPath("udstest.2.sock"), FakeHttpConnection::Type::HTTP1));
    createTestServer("test/config/integration/server_uds.json", {"http"});
  }

  /**
   * Global destructor for all integration tests.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};
