#pragma once
#include "test/integration/http_integration.h"

#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

// A test class for testing HTTP/1.1 upstream and downstreams
namespace Envoy {

namespace Network {

// TestConnectionBalancerImpl always uses first register handler, only to test extend connection
// balance.
class TestConnectionBalancerImpl : public ConnectionBalancer {
public:
  void registerHandler(BalancedConnectionHandler& handler) override {
    absl::MutexLock lock(&lock_);
    handlers_.push_back(&handler);
  }
  void unregisterHandler(BalancedConnectionHandler& handler) override {
    absl::MutexLock lock(&lock_);
    handlers_.erase(std::find(handlers_.begin(), handlers_.end(), &handler));
  }
  BalancedConnectionHandler& pickTargetHandler(BalancedConnectionHandler&) override {
    absl::MutexLock lock(&lock_);
    return *handlers_[0];
  }

private:
  absl::Mutex lock_;
  std::vector<BalancedConnectionHandler*> handlers_ ABSL_GUARDED_BY(lock_);
};

// To test extend connection balance.
class TestConnectionBalanceFactory : public ConnectionBalanceFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }
  ConnectionBalancerSharedPtr
  createConnectionBalancerFromProto(const Protobuf::Message&,
                                    Server::Configuration::FactoryContext&) override {
    return std::make_shared<TestConnectionBalancerImpl>();
  }
  std::string name() const override { return "envoy.network.connection_balance.test"; }

  std::set<std::string> configTypes() override {
    return {"envoy.config.listener.v3.Listener.ConnectionBalanceConfig.extend_balance"};
  }
};
} // namespace Network

class IntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                        public HttpIntegrationTest {
public:
  IntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}
};

class UpstreamEndpointIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public HttpIntegrationTest {
public:
  UpstreamEndpointIntegrationTest()
      : HttpIntegrationTest(
            Http::CodecType::HTTP1,
            [](int) {
              return Network::Utility::parseInternetAddress(
                  Network::Test::getLoopbackAddressString(GetParam()), 0);
            },
            GetParam()) {}
};
namespace Network {

// TestConnectionBalancerImpl always uses first register handler, only to test extend connection
// balance.
class TestConnectionBalancerImpl : public ConnectionBalancer {
public:
  void registerHandler(BalancedConnectionHandler& handler) override {
    absl::MutexLock lock(&lock_);
    handlers_.push_back(&handler);
  }
  void unregisterHandler(BalancedConnectionHandler& handler) override {
    absl::MutexLock lock(&lock_);
    handlers_.erase(std::find(handlers_.begin(), handlers_.end(), &handler));
  }
  BalancedConnectionHandler& pickTargetHandler(BalancedConnectionHandler&) override {
    absl::MutexLock lock(&lock_);
    return *handlers_[0];
  }

private:
  absl::Mutex lock_;
  std::vector<BalancedConnectionHandler*> handlers_ ABSL_GUARDED_BY(lock_);
};

// To test extend connection balance.
class TestConnectionBalanceFactory : public ConnectionBalanceFactory {
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }
  ConnectionBalancerSharedPtr
  createConnectionBalancerFromProto(const Protobuf::Message&,
                                    Server::Configuration::FactoryContext&) override {
    return std::make_shared<TestConnectionBalancerImpl>();
  }
  std::string name() const override { return "envoy.network.connection_balance.test"; }

  std::set<std::string> configTypes() override {
    return {"type.googleapis.com/network.connection_balance.test"};
  }
};
} // namespace Network
} // namespace Envoy
