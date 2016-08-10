#pragma once

#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/server.h"

#include "common/http/codec_client.h"
#include "common/stats/stats_impl.h"

class ProxyProtoIntegrationTest : public BaseIntegrationTest, public testing::Test {
public:
  /**
   * Global initializer for all integration tests.
   */
  static void SetUpTestCase() {
    test_server_ = IntegrationTestServer::create("test/config/integration/server_proxy_proto.json");
    fake_upstreams_.emplace_back(new FakeUpstream(11000, FakeHttpConnection::Type::HTTP1));
  }

  /**
   * Global destructor for all integration tests.
   */
  static void TearDownTestCase() {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};
