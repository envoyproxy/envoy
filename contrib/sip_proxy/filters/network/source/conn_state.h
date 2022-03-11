#pragma once

#include "envoy/tcp/conn_pool.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

enum class ConnectionState {
  NotConnected,
  Connecting,
  Connected,
};

/** Not used
 * SipConnectionState tracks sip-related connection state for pooled
 * connections.
 */
// class SipConnectionState : public Tcp::ConnectionPool::ConnectionState {
// public:
//  SipConnectionState(SipProxy::ConnectionState state, int32_t initial_sequence_id = 0)
//      : state_(state), next_sequence_id_(initial_sequence_id) {}
//
//  /**
//   * @return int32_t the next Sip sequence id to use for this connection.
//   */
//  int32_t nextSequenceId() {
//    if (next_sequence_id_ == std::numeric_limits<int32_t>::max()) {
//      next_sequence_id_ = 0;
//      return std::numeric_limits<int32_t>::max();
//    }
//
//    return next_sequence_id_++;
//  }
//
//  SipProxy::ConnectionState state() { return state_; }
//
// private:
//  SipProxy::ConnectionState state_;
//  int32_t next_sequence_id_;
//};

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
