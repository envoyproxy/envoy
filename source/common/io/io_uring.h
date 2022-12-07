#pragma once

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/network/address_impl.h"

namespace Envoy {
namespace Io {

/**
 * Callback invoked when iterating over entries in the completion queue.
 * @param user_data is any data attached to an entry submitted to the submission
 * queue.
 * @param result is a return code of submitted system call.
 */
using CompletionCb = std::function<void(void* user_data, int32_t result)>;

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
  virtual void forEveryCompletion(CompletionCb completion_cb) PURE;

  /**
   * Inject completion entry in the completion queue.
   */
  virtual void injectCompletion(os_fd_t fd, void* user_data, int32_t result) PURE;

  /**
   * Remove specific fd injection.
   */
  virtual void removeInjectedCompletion(os_fd_t fd) PURE;

  /**
   * Prepares an accept system call and puts it into the submission queue.
   * Returns IoUringResult::Failed in case the submission queue is full already
   * and IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareAccept(os_fd_t fd, struct sockaddr* remote_addr,
                                      socklen_t* remote_addr_len, void* user_data) PURE;

  /**
   * Prepares a connect system call and puts it into the submission queue.
   * Returns IoUringResult::Failed in case the submission queue is full already
   * and IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareConnect(os_fd_t fd,
                                       const Network::Address::InstanceConstSharedPtr& address,
                                       void* user_data) PURE;

  /**
   * Prepares a readv system call and puts it into the submission queue.
   * Returns IoUringResult::Failed in case the submission queue is full already
   * and IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareReadv(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                                     off_t offset, void* user_data) PURE;

  /**
   * Prepares a writev system call and puts it into the submission queue.
   * Returns IoUringResult::Failed in case the submission queue is full already
   * and IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                                      off_t offset, void* user_data) PURE;

  /**
   * Prepares a close system call and puts it into the submission queue.
   * Returns IoUringResult::Failed in case the submission queue is full already
   * and IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareClose(os_fd_t fd, void* user_data) PURE;

  /**
   * Prepares a cancellation and puts it into the submission queue.
   * Returns IoUringResult::Failed in case the submission queue is full already
   * and IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareCancel(void* cancelling_user_data, void* user_data) PURE;

  /**
   * Submits the entries in the submission queue to the kernel using the
   * `io_uring_enter()` system call.
   * Returns IoUringResult::Ok in case of success and may return
   * IoUringResult::Busy if we over commit the number of requests. In the latter
   * case the application should drain the completion queue by handling some completions
   * with the forEveryCompletion() method and try again.
   */
  virtual IoUringResult submit() PURE;
};

class IoUringWorker;

/**
 * Abstract factory for IoUring wrappers.
 */
class IoUringFactory {
public:
  virtual ~IoUringFactory() = default;

  virtual OptRef<IoUringWorker> getIoUringWorker() const PURE;
  /**
   * Returns an instance of IoUring for the current thread.
   */
  virtual OptRef<IoUring> get() const PURE;

  /**
   * Initializes a factory upon server readiness. For example this method can be
   * used to set TLS.
   */
  virtual void onServerInitialized() PURE;

  virtual bool currentThreadRegistered() PURE;
};

enum class RequestType { Accept, Connect, Read, Write, Close, Cancel, Unknown };

class IoUringHandler;
class IoUringSocket;

struct Request {
  RequestType type_{RequestType::Unknown};
  absl::optional<std::reference_wrapper<IoUringSocket>> io_uring_socket_{absl::nullopt};
};

struct AcceptedSocketParam {
  os_fd_t fd_;
  sockaddr_storage* remote_addr_;
  socklen_t remote_addr_len_;
};

struct ReadParam {
  Buffer::Instance& pending_read_buf_;
  int32_t result_;
};

struct WriteParam {
  int32_t result_;
};

class IoUringSocket : public Event::DeferredDeletable {
public:
  virtual ~IoUringSocket() = default;

  virtual os_fd_t fd() const PURE;
  virtual void start() PURE;
  virtual void close() PURE;
  virtual void enable() PURE;
  virtual void disable() PURE;

  virtual uint64_t write(Buffer::Instance&) { PANIC("not implemented"); }
  virtual uint64_t writev(const Buffer::RawSlice*, uint64_t) { PANIC("not implemented"); }

  virtual void connect(const Network::Address::InstanceConstSharedPtr&) {
    PANIC("not implemented");
  }

  virtual void onAccept(int32_t) { PANIC("not implemented"); };
  virtual void onClose(int32_t) { PANIC("not implemented"); };
  virtual void onCancel(int32_t) { PANIC("not implemented"); };
  virtual void onConnect(int32_t) { PANIC("not implemented"); };
  virtual void onRead(int32_t) { PANIC("not implemented"); };
  virtual void onWrite(int32_t, Request*) { PANIC("not implemented"); };
};

class IoUringWorker : public ThreadLocal::ThreadLocalObject {
public:
  virtual ~IoUringWorker() = default;

  virtual void start() PURE;

  virtual void enableSocket(os_fd_t fd) PURE;
  virtual void disableSocket(os_fd_t fd) PURE;
  virtual IoUringSocket& addAcceptSocket(os_fd_t fd, IoUringHandler& handler) PURE;
  virtual IoUringSocket& addServerSocket(os_fd_t fd, IoUringHandler& handler,
                                         uint32_t read_buffer_size) PURE;
  virtual IoUringSocket& addClientSocket(os_fd_t fd, IoUringHandler& handler,
                                         uint32_t read_buffer_size) PURE;
  virtual void closeSocket(os_fd_t fd) PURE;
  virtual std::unique_ptr<IoUringSocket> removeSocket(os_fd_t) PURE;
  virtual Event::Dispatcher& dispatcher() PURE;
  virtual IoUringSocket& getIoUringSocket(os_fd_t fd) PURE;

  virtual Request* submitAcceptRequest(IoUringSocket& socket, sockaddr_storage* remote_addr,
                                       socklen_t* remote_addr_len) PURE;
  virtual Request* submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) PURE;
  virtual Request* submitCloseRequest(IoUringSocket& socket) PURE;
  virtual Request* submitReadRequest(IoUringSocket& socket, struct iovec* iov) PURE;
  virtual Request* submitWritevRequest(IoUringSocket& socket, struct iovec* iovecs,
                                       uint64_t num_vecs) PURE;
  virtual Request*
  submitConnectRequest(IoUringSocket& socket,
                       const Network::Address::InstanceConstSharedPtr& address) PURE;

  virtual void injectCompletion(os_fd_t, RequestType type, int32_t result) PURE;
  virtual void injectCompletion(IoUringSocket& socket, RequestType type, int32_t result) PURE;

  virtual IoUring& get() PURE;
};

class IoUringHandler {
public:
  virtual ~IoUringHandler() = default;

  virtual void onAcceptSocket(AcceptedSocketParam& param) PURE;
  virtual void onRead(ReadParam& param) PURE;
  virtual void onWrite(WriteParam& param) PURE;
};

} // namespace Io
} // namespace Envoy
