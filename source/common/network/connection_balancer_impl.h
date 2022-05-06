#pragma once

#include "envoy/network/connection_balancer.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Network {

class ConnectionBalanceFactory : public Config::TypedFactory {
public:
  ~ConnectionBalanceFactory() override = default;
  std::string category() const override { return "envoy.network.connectionbalance"; }
};

/**
 * Lookup ConnectionBalancer instance by name
 * @param name Name of the connection balancer to be looked up
 * @return Pointer to @ref ConnectionBalancer instance that registered using the name of nullptr
 */
static inline ConnectionBalancerSharedPtr connectionBalancer(std::string name) {
  auto factory =
      Envoy::Registry::FactoryRegistry<Envoy::Network::ConnectionBalanceFactory>::getFactory(name);
  return std::shared_ptr<ConnectionBalancer>(reinterpret_cast<ConnectionBalancer*>(factory));
}

class ConnectionBalancerBase : public ConnectionBalancer, public ConnectionBalanceFactory {};

/**
 * Implementation of connection balancer that does exact balancing. This means that a lock is held
 * during balancing so that connection counts are nearly exactly balanced between handlers. This
 * is "nearly" exact in the sense that a connection might close in parallel thus making the counts
 * incorrect, but this should be rectified on the next accept. This balancer sacrifices accept
 * throughput for accuracy and should be used when there are a small number of connections that
 * rarely cycle (e.g., service mesh gRPC egress).
 */
class ExactConnectionBalancerImpl : public ConnectionBalancerBase {
public:
  // ConnectionBalancer
  void registerHandler(BalancedConnectionHandler& handler) override;
  void unregisterHandler(BalancedConnectionHandler& handler) override;
  BalancedConnectionHandler& pickTargetHandler(BalancedConnectionHandler& current_handler) override;

  std::string name() const override {
    return "envoy.config.listener.v3.Listener.ConnectionBalanceConfig.ExactBalance";
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::config::listener::v3::Listener::ConnectionBalanceConfig::ExactBalance>();
  }

private:
  absl::Mutex lock_;
  std::vector<BalancedConnectionHandler*> handlers_ ABSL_GUARDED_BY(lock_);
};

/**
 * A NOP connection balancer implementation that always continues execution after incrementing
 * the handler's connection count.
 */
class NopConnectionBalancerImpl : public ConnectionBalancerBase {
public:
  // ConnectionBalancer
  void registerHandler(BalancedConnectionHandler&) override {}
  void unregisterHandler(BalancedConnectionHandler&) override {}
  BalancedConnectionHandler&
  pickTargetHandler(BalancedConnectionHandler& current_handler) override {
    // In the NOP case just increment the connection count and return the current handler.
    current_handler.incNumConnections();
    return current_handler;
  }

  std::string name() const override {
    return "envoy.config.listener.v3.Listener.ConnectionBalanceConfig.NopBalance";
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::config::listener::v3::Listener::ConnectionBalanceConfig::NopBalance>();
  }
};

} // namespace Network
} // namespace Envoy
