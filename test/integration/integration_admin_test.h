#pragma once

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

class IntegrationAdminTest : public HttpIntegrationTest,
                             public testing::TestWithParam<Network::Address::IpVersion> {
public:
  IntegrationAdminTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initialize() override {
    config_helper_.addFilter(ConfigHelper::DEFAULT_HEALTH_CHECK_FILTER);
    HttpIntegrationTest::initialize();
  }

  /**
   *  Destructor for an individual test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};

} // namespace Envoy
