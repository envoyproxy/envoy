#pragma once

#include <string>
#include <vector>

#include "extensions/filters/network/common/redis/codec_impl.h"

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

class TestDecoderCallbacks : public Extensions::NetworkFilters::Common::Redis::DecoderCallbacks {
public:
  TestDecoderCallbacks() = default;
  ~TestDecoderCallbacks() = default;

  void onRespValue(Extensions::NetworkFilters::Common::Redis::RespValuePtr&& value) {
    decoded_ = std::move(value);
  }

  Extensions::NetworkFilters::Common::Redis::RespValuePtr& decoded() { return decoded_; }

private:
  Extensions::NetworkFilters::Common::Redis::RespValuePtr decoded_;
};

} // namespace
} // namespace Envoy
