#pragma once

#include <memory>

#include "envoy/server/instance.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"

#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {

class AdminResponse;

// Holds context for a streaming response from the admin system, enabling
// flow-control into another system. This is particularly important when the
// generated response is very large, such that holding it in memory may cause
// fragmentation or out-of-memory failures. It is possible to interleave xDS
// response handling, overload management, and other admin requests during the
// streaming of a long admin response.
//
// There can be be multiple AdminResponses at a time; each are separately
// managed. However they will obtain their data from Envoy functions that
// run on the main thread.
//
// Responses may still be active after the server has shut down, and is no
// longer running its main thread dispatcher. In this state, the callbacks
// will be called with appropriate error codes.
//
// Requests can also be cancelled explicitly by calling cancel(). After
// cancel() is called, no further callbacks will be called by the response.
//
// The lifecycle of an AdminResponse is rendered as a finite state machine
// bubble diagram:
// https://docs.google.com/drawings/d/1njUl1twApEMoxmjaG4b7optTh5fcb_YNcfSnkHbdfq0/view
class AdminResponse : public std::enable_shared_from_this<AdminResponse> {
public:
  // AdminResponse can outlive MainCommonBase. But AdminResponse needs a
  // reliable way of knowing whether MainCommonBase is alive, so we do this with
  // PtrSet, which is held by MainCommonBase and all the active AdminResponses.
  // via shared_ptr. This gives MainCommonBase a reliable way of notifying all
  // active responses that it is being shut down, and thus all responses need to
  // be terminated. And it gives a reliable way for AdminResponse to detach
  // itself, whether or not MainCommonBase is already deleted.
  //
  // In summary:
  //  * MainCommonBase can outlive AdminResponse so we need detachResponse.
  //  * AdminResponse can outlive MainCommonBase, so we need shared_ptr.
  class PtrSet {
  public:
    /**
     * Called when an AdminResponse is created. When terminateAdminRequests is
     * called, all outstanding response objects have their terminate() methods
     * called.
     *
     * @param response the response pointer to be added to the set.
     */
    void attachResponse(AdminResponse* response);

    /**
     * Called when an AdminResponse is terminated, either by completing normally
     * or having the caller call cancel on it. Either way it needs to be removed
     * from the set that will be used by terminateAdminRequests below.
     *
     * @param response the response pointer to be removed from the set.
     */
    void detachResponse(AdminResponse* response);

    /**
     * Called after the server run-loop finishes; any outstanding streaming
     * admin requests will otherwise hang as the main-thread dispatcher loop
     * will no longer run.
     */
    void terminateAdminRequests();

    mutable absl::Mutex mutex_;
    absl::flat_hash_set<AdminResponse*> response_set_ ABSL_GUARDED_BY(mutex_);
    bool accepting_admin_requests_ ABSL_GUARDED_BY(mutex_) = true;
  };
  using SharedPtrSet = std::shared_ptr<PtrSet>;

  AdminResponse(Server::Instance& server, absl::string_view path, absl::string_view method,
                SharedPtrSet response_set);
  ~AdminResponse();

  /**
   * Requests the headers for the response. This can be called from any
   * thread, and HeaderFn may also be called from any thread.
   *
   * HeadersFn will not be called after cancel(). It is invalid to
   * to call nextChunk from within HeadersFn -- the caller must trigger
   * such a call on another thread, after HeadersFn returns. Calling
   * nextChunk from HeadersFn may deadlock.
   *
   * If the server is shut down during the operation, headersFn may
   * be called with a 503, if it has not already been called.
   *
   * @param fn The function to be called with the headers and status code.
   */
  using HeadersFn = std::function<void(Http::Code, Http::ResponseHeaderMap& map)>;
  void getHeaders(HeadersFn fn);

  /**
   * Requests a new chunk. This can be called from any thread, and the BodyFn
   * callback may also be called from any thread. BodyFn will be called in a
   * loop until the Buffer passed to it is fully drained. When 'false' is
   * passed as the second arg to BodyFn, that signifies the end of the
   * response, and nextChunk must not be called again.
   *
   * BodyFn will not be called after cancel(). It is invalid to
   * to call nextChunk from within BodyFn -- the caller must trigger
   * such a call on another thread, after BodyFn returns. Calling
   * nextChunk from BodyFn may deadlock.
   *
   * If the server is shut down during the operation, bodyFn will
   * be called with an empty body and 'false' for more_data, if
   * this has not already occurred.
   *
   * @param fn A function to be called on each chunk.
   */
  using BodyFn = std::function<void(Buffer::Instance&, bool)>;
  void nextChunk(BodyFn fn);

  /**
   * Requests that any outstanding callbacks be dropped. This can be called
   * when the context in which the request is made is destroyed. This enables
   * an application to implement a. The Response itself is held as a
   * shared_ptr as that makes it much easier to manage cancellation across
   * multiple threads.
   */
  void cancel();

  /**
   * @return whether the request was cancelled.
   */
  bool cancelled() const;

private:
  /**
   * Called when the server is terminated. This calls any outstanding
   * callbacks to be called. If nextChunk is called after termination,
   * its callback is called false for the second arg, indicating
   * end of stream.
   */
  void terminate();

  void requestHeaders();
  void requestNextChunk();
  void sendAbortChunkLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void sendErrorLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  Server::Instance& server_;
  OptRef<Server::Admin> opt_admin_;
  Buffer::OwnedImpl response_;
  Http::Code code_;
  Server::Admin::RequestPtr request_;
  Http::RequestHeaderMapPtr request_headers_{Http::RequestHeaderMapImpl::create()};
  Http::ResponseHeaderMapPtr response_headers_{Http::ResponseHeaderMapImpl::create()};
  bool more_data_ = true;

  // True if cancel() was explicitly called by the user; headers and body
  // callbacks are never called after cancel().
  bool cancelled_ ABSL_GUARDED_BY(mutex_) = false;

  // True if the Envoy server has stopped running its main loop. Headers and
  // body requests can be initiated and called back are called after terminate,
  // so callers do not have to special case this -- the request will simply fail
  // with an empty response.
  bool terminated_ ABSL_GUARDED_BY(mutex_) = false;

  // Used to indicate whether the body function has been called with false
  // as its second argument. That must always happen at most once, even
  // if terminate races with the normal end-of-stream marker. more=false
  // may never be sent if the request is cancelled, nor deleted prior to
  // it being requested.
  bool sent_end_stream_ ABSL_GUARDED_BY(mutex_) = false;

  HeadersFn headers_fn_ ABSL_GUARDED_BY(mutex_);
  BodyFn body_fn_ ABSL_GUARDED_BY(mutex_);
  mutable absl::Mutex mutex_;

  SharedPtrSet shared_response_set_;
};
using AdminResponseSharedPtr = std::shared_ptr<AdminResponse>;

} // namespace Envoy
