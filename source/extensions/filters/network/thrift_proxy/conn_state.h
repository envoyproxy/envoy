#pragma once

#include "envoy/tcp/conn_pool.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * ThriftConnectionState tracks thrift-related connection state for pooled connections.
 */
class ThriftConnectionState : public Tcp::ConnectionPool::ConnectionState {
public:
  /**
   * @return int32_t the next Thrift sequence id to use for this connection.
   */
  int32_t nextSequenceId() { return next_sequence_id_++; }

  /**
   * @return true if this upgrade has been attempted on this connection.
   */
  bool upgradeAttempted() const { return upgrade_attempted_; }
  /**
   * @return true if this connection has been upgraded
   */
  bool isUpgraded() const { return upgraded_; }

  /**
   * Marks the connection as successfully upgraded.
   */
  void markUpgraded() {
    upgrade_attempted_ = true;
    upgraded_ = true;
  }

  /**
   * Marks the connection as not upgraded.
   */
  void markUpgradeFailed() {
    upgrade_attempted_ = true;
    upgraded_ = false;
  }

private:
  int32_t next_sequence_id_{0};
  bool upgrade_attempted_{false};
  bool upgraded_{false};
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
