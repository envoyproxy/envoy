#pragma once

#include "common/http/codec_client.h"
#include "common/stats/stats_impl.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/server.h"

#include "gtest/gtest.h"

class ProxyProtoIntegrationTest : public BaseIntegrationTest, public testing::Test {
public:
  /**
   * Initializer for an individual test.
   */
  void SetUp() override {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1));
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    createTestServer("test/config/integration/server_proxy_proto.json", {"http"});
  }

  /**
   * Destructor for an individual test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};
