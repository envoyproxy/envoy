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

  //! @return The number of streams waiting to be processed after being assigned a pool
  size_t numWaitingStreams() const;

  //! @return The number of wrappers in the pending state.
  size_t numPendingStreams() const;

private:
  //! This lets us invoke different cancellation logic depending on in what stage of a request
  //! lifetime we're in. For example, on an initial request, we may just want to remove from the
  //! pending list. However, later we may want to remove from a pending list elsewhere, due to the
  //! ownership of the request actually changing. This lets us do so while keeping the original
  //! cancellable object in tact for the original caller.
  class PendingWrapper : public LinkedObject<PendingWrapper>,
                         public ConnectionPool::Cancellable,
                         public ConnectionPool::Callbacks {
  public:
    PendingWrapper(Http::StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks,
                   const Upstream::LoadBalancerContext& context, WrappedConnectionPool& parent);
    ~PendingWrapper();

    //! ConnectionPool::Cancellable
    void cancel() override;

    //! ConnectionPool::Callbacks
    void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                       Upstream::HostDescriptionConstSharedPtr host) override;

    void onPoolReady(Http::StreamEncoder& encoder,
                     Upstream::HostDescriptionConstSharedPtr host) override;

    void setPendingRequest(ConnPoolImplBase::PendingRequest& request) {
      wrapped_pending_ = &request;
    }

    void setWaitingCancelCallback(ConnectionPool::Cancellable& cancellable) {
      waiting_cancel_ = &cancellable;
    }

    //! Tries to allocate a pending request if there is one.
    //! @param mapper The mapper to use to allocate the pool for the request
    //! @param pending_list the list owning any pending requests so we can cleanup when done.
    //! @return true if a request was allocated to a pool. False otherwise.
    bool allocatePending(ConnectionMapper& mapper,
                         std::list<ConnPoolImplBase::PendingRequestPtr>& pending_list);

  private:
    Http::StreamDecoder& decoder_;
    ConnectionPool::Callbacks& wrapped_callbacks_;
    const Upstream::LoadBalancerContext& context_;
    ConnPoolImplBase::PendingRequest* wrapped_pending_;
    ConnectionPool::Cancellable* waiting_cancel_;
    WrappedConnectionPool& parent_;
  };

  using PendingWrapperPtr = std::unique_ptr<PendingWrapper>;

  /*
   * Stores a connection request for later processing, if possible.
   *
   * @ return A handle for possible canceling of the pending request.
   */
  ConnectionPool::Cancellable* pushPending(std::unique_ptr<PendingWrapper> wrapper,
                                           Http::StreamDecoder& response_decoder,
                                           ConnectionPool::Callbacks& callbacks,
                                           const Upstream::LoadBalancerContext& context);

  //! @return true if there is nothing going on so we can drain any connections
  bool drainable() const;

  //! Tries to allocate any pending requests
  void allocatePendingRequests();

  //! Called when a wrapped request has been cancelled so we can free it up.
  void onWrappedRequestPendingCancel(PendingWrapper& wrapper);

  //! Called when a wrapped request has either been cancelled, or finished waiting.
  void onWrappedRequestWaitingFinished(PendingWrapper& wrapper);

  std::unique_ptr<ConnectionMapper> mapper_;
  Protocol protocol_;
  std::vector<DrainedCb> drained_callbacks_;
  std::list<PendingWrapperPtr> wrapped_pending_;
  std::list<PendingWrapperPtr> wrapped_waiting_;
};

} // namespace Http
} // namespace Envoy
