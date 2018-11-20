#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "envoy/http/conn_pool.h"
#include "envoy/http/connection_mapper.h"

#include "common/common/assert.h"
#include "common/http/conn_pool_base.h"

namespace Envoy {
namespace Http {

class WrappedConnectionPool : public ConnectionPool::Instance, public ConnPoolImplBase {
public:
  WrappedConnectionPool(std::unique_ptr<ConnectionMapper> mapper, Protocol protocol,
                        Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority);
  Protocol protocol() const override;
  void addDrainedCallback(DrainedCb cb) override;
  void drainConnections() override;
  ConnectionPool::Cancellable* newStream(Http::StreamDecoder& /* unused */,
                                         ConnectionPool::Callbacks& /* unused */) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  ConnectionPool::Cancellable* newStream(Http::StreamDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks,
                                         const Upstream::LoadBalancerContext& context) override;

  // ConnPoolImplBase
  void checkForDrained() override;

  //! @ return the number of streams waiting to be assigned to a pool
  size_t numPendingStreams() const;

private:
  /*
   * Stores a connection request for later processing, if possible.
   *
   * @ return A handle for possible canceling of the pending request.
   */
  ConnectionPool::Cancellable* pushPending(Http::StreamDecoder& response_decoder,
                                           ConnectionPool::Callbacks& callbacks,
                                           const Upstream::LoadBalancerContext& context);

  //! @return true if there is nothing going on so we can drain any connections
  bool drainable() const;

  std::unique_ptr<ConnectionMapper> mapper_;
  Protocol protocol_;
  std::vector<DrainedCb> drained_callbacks_;
};

} // namespace Http
} // namespace Envoy
