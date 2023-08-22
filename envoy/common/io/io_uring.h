#pragma once

#include <functional>

#include "envoy/common/pure.h"
#include "envoy/network/address.h"

namespace Envoy {
namespace Io {

/**
 * Abstract for io_uring I/O Request.
 */
class Request {
public:
  virtual ~Request() = default;
};

/**
 * Callback invoked when iterating over entries in the completion queue.
 * @param user_data is any data attached to an entry submitted to the submission
 * queue.
 * @param result is a return code of submitted system call.
 * @param injected indicates whether the completion is injected or not.
 */
using CompletionCb = std::function<void(Request* user_data, int32_t result, bool injected)>;

/**
 * Callback for releasing the user data.
 * @param user_data the pointer to the user data.
 */
using InjectedCompletionUserDataReleasor = std::function<void(Request* user_data)>;

enum class IoUringResult { Ok, Busy, Failed };

/**
 * Abstract wrapper around `io_uring`.
 */
class IoUring {
public:
  virtual ~IoUring() = default;

  /**
   * Registers an eventfd file descriptor for the ring and returns it.
   * It can be used for integration with event loops.
   */
  virtual os_fd_t registerEventfd() PURE;

  /**
   * Resets the eventfd file descriptor for the ring.
   */
  virtual void unregisterEventfd() PURE;

  /**
   * Returns true if an eventfd file descriptor is registered with the ring.
   */
  virtual bool isEventfdRegistered() const PURE;

  /**
   * Iterates over entries in the completion queue, calls the given callback for
   * every entry and marks them consumed.
   */
  virtual void forEveryCompletion(const CompletionCb& completion_cb) PURE;

  /**
   * Prepares an accept system call and puts it into the submission queue.
   * Returns IoUringResult::Failed in case the submission queue is full already
   * and IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareAccept(os_fd_t fd, struct sockaddr* remote_addr,
                                      socklen_t* remote_addr_len, Request* user_data) PURE;

  /**
   * Prepares a connect system call and puts it into the submission queue.
   * Returns IoUringResult::Failed in case the submission queue is full already
   * and IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareConnect(os_fd_t fd,
                                       const Network::Address::InstanceConstSharedPtr& address,
                                       Request* user_data) PURE;

  /**
   * Prepares a readv system call and puts it into the submission queue.
   * Returns IoUringResult::Failed in case the submission queue is full already
   * and IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareReadv(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                                     off_t offset, Request* user_data) PURE;

  /**
   * Prepares a writev system call and puts it into the submission queue.
   * Returns IoUringResult::Failed in case the submission queue is full already
   * and IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                                      off_t offset, Request* user_data) PURE;

  /**
   * Prepares a close system call and puts it into the submission queue.
   * Returns IoUringResult::Failed in case the submission queue is full already
   * and IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareClose(os_fd_t fd, Request* user_data) PURE;

  /**
   * Submits the entries in the submission queue to the kernel using the
   * `io_uring_enter()` system call.
   * Returns IoUringResult::Ok in case of success and may return
   * IoUringResult::Busy if we over commit the number of requests. In the latter
   * case the application should drain the completion queue by handling some completions
   * with the forEveryCompletion() method and try again.
   */
  virtual IoUringResult submit() PURE;

  /**
   * Inject a request completion into the io_uring. Those completions will be iterated
   * when calling the `forEveryCompletion`. This is used to inject an emulated iouring
   * request completion by the upper-layer, then trigger the request completion processing.
   * it is used to emulate an activation of READ/WRITE/CLOSED event on the specific file
   * descriptor by the IoSocketHandle.
   * @param fd is the file descriptor of this completion refer to.
   * @param user_data is the user data related to this completion.
   * @param result is request result for this completion.
   */
  virtual void injectCompletion(os_fd_t fd, Request* user_data, int32_t result) PURE;

  /**
   * Remove all the injected completions for the specific file descriptor. This is used
   * to cleanup all the injected completions when a socket closed and remove from the iouring.
   * @param fd is used to refer to the completions will be removed.
   */
  virtual void removeInjectedCompletion(os_fd_t fd) PURE;
};

using IoUringPtr = std::unique_ptr<IoUring>;

/**
 * Abstract factory for IoUring wrappers.
 */
class IoUringFactory {
public:
  virtual ~IoUringFactory() = default;

  /**
   * Returns an instance of IoUring and creates it if needed for the current
   * thread.
   */
  virtual IoUring& getOrCreate() const PURE;

  /**
   * Initializes a factory upon server readiness. For example this method can be
   * used to set TLS.
   */
  virtual void onServerInitialized() PURE;
};

} // namespace Io
} // namespace Envoy
