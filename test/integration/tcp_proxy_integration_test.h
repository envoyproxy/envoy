#pragma once

#include <memory>
#include <string>

#include "test/integration/integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {
class TcpProxyIntegrationTest : public BaseIntegrationTest,
                                public testing::TestWithParam<Network::Address::IpVersion> {
public:
  TcpProxyIntegrationTest() : BaseIntegrationTest(GetParam(), ConfigHelper::TCP_PROXY_CONFIG) {}

  void initialize() override {
    named_ports_ = {{"tcp_proxy"}};
    BaseIntegrationTest::initialize();
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                             const std::string& data_to_send_downstream);
};
} // namespace
} // namespace Envoy
