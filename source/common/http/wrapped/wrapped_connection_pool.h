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

  void setUpstreamSourceInformation(
      const ConnectionPool::UpstreamSourceInformation& /* unused */) override {

    // Unimplemented for now. We may want to look at using this as a way of doing a "union" of the
    // source information when multiple dimensions are involved -- i.e. chain multiple wrapped pools
    // together.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

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
  class StreamWrapper : public LinkedObject<StreamWrapper>,
                        public ConnectionPool::Cancellable,
                        public ConnectionPool::Callbacks,
                        Logger::Loggable<Logger::Id::http> {
  public:
    StreamWrapper(Http::StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks,
                  const Upstream::LoadBalancerContext& context, WrappedConnectionPool& parent);
    ~StreamWrapper();

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

    //! Assigns a new stream in the given pool with the wrapped stream parameters.
    //! Note: *this should be considered "waiting" at this point, since invoking this function
    //! may invalidate it, which will assume it is in the waiting state.
    //! @param pool the pool on which to assign the new stream
    //! @return the result of creating the new stream. May be nullptr with all the semantics of
    //!         ConnectionPool::Instance::newStream.
    ConnectionPool::Cancellable* newStreamWrapped(ConnectionPool::Instance& pool);

    //! Tries to allocate a pending request if there is one.
    //! @param mapper The mapper to use to allocate the pool for the request
    //! @param pending_list the list owning any pending requests so we can cleanup when done.
    //! @return The connection pool one was allocated. False otherwise.
    ConnectionPool::Instance*
    allocatePending(ConnectionMapper& mapper,
                    std::list<ConnPoolImplBase::PendingRequestPtr>& pending_list);

  private:
    Http::StreamDecoder& decoder_;
    ConnectionPool::Callbacks& wrapped_callbacks_;
    const Upstream::LoadBalancerContext& context_;
    ConnPoolImplBase::PendingRequest* wrapped_pending_;
    ConnectionPool::Cancellable* waiting_cancel_;
    WrappedConnectionPool& parent_;
  };

  using StreamWrapperPtr = std::unique_ptr<StreamWrapper>;

  /*
   * Stores a connection request for later processing, if possible.
   *
   * @ return A handle for possible canceling of the pending request.
   */
  ConnectionPool::Cancellable* pushPending(std::unique_ptr<StreamWrapper> wrapper,
                                           Http::StreamDecoder& response_decoder,
                                           ConnectionPool::Callbacks& callbacks,
                                           const Upstream::LoadBalancerContext& context);

  //! @return true if there is nothing going on so we can drain any connections
  bool drainable() const;

  //! Tries to allocate any pending requests
  void allocatePendingRequests();

  //! Called when a wrapped request has been cancelled so we can free it up.
  void onWrappedRequestPendingCancel(StreamWrapper& wrapper);

  //! Called when a wrapped request has either been cancelled, or finished waiting.
  void onWrappedRequestWaitingFinished(StreamWrapper& wrapper);

  std::unique_ptr<ConnectionMapper> mapper_;
  Protocol protocol_;
  std::vector<DrainedCb> drained_callbacks_;
  std::list<StreamWrapperPtr> wrapped_pending_;
  std::list<StreamWrapperPtr> wrapped_waiting_;
};

} // namespace Http
} // namespace Envoy
