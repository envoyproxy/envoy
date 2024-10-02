
#pragma once

#include <chrono>

#include "envoy/common/conn_pool.h"

#include "source/common/common/linked_object.h"

namespace Envoy {
namespace ConnectionPool {

class ConnPoolImplBase;
struct AttachContext;

// PendingStream is the base class tracking streams for which a connection has been created but not
// yet established.
class PendingStream : public LinkedObject<PendingStream>, public ConnectionPool::Cancellable {
public:
  PendingStream(ConnPoolImplBase& parent, bool can_send_early_data);
  ~PendingStream() override;

  // ConnectionPool::Cancellable
  void cancel(Envoy::ConnectionPool::CancelPolicy policy) override;

  // The context here returns a pointer to whatever context is provided with newStream(),
  // which will be passed back to the parent in onPoolReady or onPoolFailure.
  virtual AttachContext& context() PURE;

  std::chrono::time_point<std::chrono::steady_clock> enqueuedTimestamp() const {
    return enqueued_ts_;
  }

  ConnPoolImplBase& parent_;
  // The request can be sent as early data.
  bool can_send_early_data_;
  // Timestamp at which the stream was enqueued.
  std::chrono::time_point<std::chrono::steady_clock> enqueued_ts_;
};

using PendingStreamPtr = std::unique_ptr<PendingStream>;

} // namespace ConnectionPool
} // namespace Envoy