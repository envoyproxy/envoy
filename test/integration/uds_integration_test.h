#pragma once

#include "common/http/codec_client.h"
#include "common/stats/stats_impl.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/server.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
class UdsIntegrationTest : public BaseIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  UdsIntegrationTest() : BaseIntegrationTest(GetParam()) {}
  /**
   * Initializer for an individual test.
   */
  void SetUp() override {
    fake_upstreams_.emplace_back(new FakeUpstream(
        TestEnvironment::unixDomainSocketPath("udstest.1.sock"), FakeHttpConnection::Type::HTTP1));
    fake_upstreams_.emplace_back(new FakeUpstream(
        TestEnvironment::unixDomainSocketPath("udstest.2.sock"), FakeHttpConnection::Type::HTTP1));
    createTestServer("test/config/integration/server_uds.json", {"http"});
  }

  /**
   * Destructor for an individual test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};
} // namespace Envoy
