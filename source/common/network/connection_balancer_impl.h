#pragma once

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/network/connection_balancer.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Network {
/**
 * A base factory to create connection balance, which makes it easier to extend.
 */
class ConnectionBalanceFactory : public Config::TypedFactory {
public:
  ~ConnectionBalanceFactory() override = default;
  virtual ConnectionBalancerSharedPtr
  createConnectionBalancerFromProto(const Protobuf::Message& config,
                                    Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.network.connection_balance"; }
};

/**
 * Implementation of connection balancer that does exact balancing. This means that a lock is held
 * during balancing so that connection counts are nearly exactly balanced between handlers. This
 * is "nearly" exact in the sense that a connection might close in parallel thus making the counts
 * incorrect, but this should be rectified on the next accept. This balancer sacrifices accept
 * throughput for accuracy and should be used when there are a small number of connections that
 * rarely cycle (e.g., service mesh gRPC egress).
 */
class ExactConnectionBalancerImpl : public ConnectionBalancer {
public:
  // ConnectionBalancer
  void registerHandler(BalancedConnectionHandler& handler) override;
  void unregisterHandler(BalancedConnectionHandler& handler) override;
  BalancedConnectionHandler& pickTargetHandler(BalancedConnectionHandler& current_handler) override;

private:
  absl::Mutex lock_;
  std::vector<BalancedConnectionHandler*> handlers_ ABSL_GUARDED_BY(lock_);
};

/**
 * A NOP connection balancer implementation that always continues execution after incrementing
 * the handler's connection count.
 */
class NopConnectionBalancerImpl : public ConnectionBalancer {
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
};

} // namespace Network
} // namespace Envoy
