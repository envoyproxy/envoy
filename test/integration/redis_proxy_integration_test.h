#pragma once

#include "test/integration/integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {
class RedisProxyIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public BaseIntegrationTest {
public:
  RedisProxyIntegrationTest() : BaseIntegrationTest(GetParam(), ConfigHelper::REDIS_PROXY_CONFIG) {}

  ~RedisProxyIntegrationTest() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void initialize() override;
};

} // namespace
} // namespace Envoy
