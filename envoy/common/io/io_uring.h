#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/io/io_uring.h"
#include "envoy/common/pure.h"
#include "envoy/network/address.h"
#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace Io {

/**
 * Callback invoked when iterating over entries in the completion queue.
 * @param user_data is any data attached to an entry submitted to the submission
 * queue.
 * @param result is a return code of submitted system call.
 * @param injected indicated the completion is injected or not.
 */
using CompletionCb = std::function<void(void* user_data, int32_t result, bool injected)>;

enum class IoUringResult { Ok, Busy, Failed };

/**
 * Abstract wrapper around `io_uring`.
 */
class IoUring {
public:
  virtual ~IoUring() = default;

  /**
   * Registers an eventfd file descriptor for the ring and returns it.
   * It can be used for integration with event loops. The assertion is
   * the eventfd isn't registered.
   */
  virtual os_fd_t registerEventfd() PURE;

  /**
   * Resets the eventfd file descriptor for the ring. The assertion is
   * the eventfd is registered.
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

  /**
   * Inject a request completion into the io_uring.
   */
  virtual void injectCompletion(os_fd_t fd, void* user_data, int32_t result) PURE;

  /**
   * Remove the injected completion for specific fd.
   */
  virtual void removeInjectedCompletion(os_fd_t fd) PURE;
};

/**
 * io_uring request type.
 */
struct RequestType {
  static constexpr uint32_t Accept = 0x1;
  static constexpr uint32_t Connect = 0x2;
  static constexpr uint32_t Read = 0x4;
  static constexpr uint32_t Write = 0x8;
  static constexpr uint32_t Close = 0x10;
  static constexpr uint32_t Cancel = 0x20;
};

class IoUringSocket;

/**
 * Abstract for io_uring I/O Request.
 */
struct Request {
  uint32_t type_;
  IoUringSocket& io_uring_socket_;
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

/**
 * Abstract for each socket.
 */
class IoUringSocket {
public:
  virtual ~IoUringSocket() = default;

  /**
   * Return the raw fd.
   */
  virtual os_fd_t fd() const PURE;

  /**
   * close the socket.
   */
  virtual void close() PURE;

  /**
   * Enable the socket.
   */
  virtual void enable() PURE;

  /**
   * Disable the socket.
   */
  virtual void disable() PURE;

  /**
   * Write data to the socket.
   */
  virtual uint64_t write(Buffer::Instance& data) PURE;
  virtual uint64_t writev(const Buffer::RawSlice*, uint64_t) PURE;

  /**
   * Connect to an address.
   * @param address the peer of address which is connected to.
   */
  virtual void connect(const Network::Address::InstanceConstSharedPtr& address) PURE;

  /**
   * On accept request completed.
   * @param result the result of operation in the request.
   * @param injected indicates the completion is injected or not.
   */
  virtual void onAccept(int32_t result, bool injected) PURE;

  /**
   * On close request completed.
   * @param result the result of operation in the request.
   * @param injected indicates the completion is injected or not.
   */
  virtual void onClose(int32_t result, bool injected) PURE;

  /**
   * On cancel request completed.
   * @param result the result of operation in the request.
   * @param injected indicates the completion is injected or not.
   */
  virtual void onCancel(int32_t result, bool injected) PURE;

  /**
   * On connect request completed.
   * @param result the result of operation in the request.
   * @param injected indicates the completion is injected or not.
   */
  virtual void onConnect(int32_t result, bool injected) PURE;

  /**
   * On read request completed.
   * @param result the result of operation in the request.
   * @param injected indicates the completion is injected or not.
   */
  virtual void onRead(int32_t result, bool injected) PURE;

  /**
   * On write request completed.
   * @param result the result of operation in the request.
   * @param injected indicates the completion is injected or not.
   */
  virtual void onWrite(int32_t result, bool injected) PURE;

  /**
   * Inject a request completion to the io uring instance.
   * @param type the request type of injected completion.
   */
  virtual void injectCompletion(uint32_t type) PURE;
};

/**
 * The handler for io_uring request completion.
 */
class IoUringHandler {
public:
  virtual ~IoUringHandler() = default;

  virtual void onAcceptSocket(AcceptedSocketParam& param) PURE;
  virtual void onRead(ReadParam& param) PURE;
  virtual void onWrite(WriteParam& param) PURE;
};

/**
 * Abstract for per-thread worker.
 */
class IoUringWorker : public ThreadLocal::ThreadLocalObject {
public:
  ~IoUringWorker() override = default;

  /**
   * Add an accept socket socket to the worker.
   */
  virtual IoUringSocket& addAcceptSocket(os_fd_t fd, IoUringHandler& handler) PURE;

  /**
   * Add an server socket socket to the worker.
   */
  virtual IoUringSocket& addServerSocket(os_fd_t fd, IoUringHandler& handler,
                                         uint32_t read_buffer_size) PURE;

  /**
   * Add an client socket socket to the worker.
   */
  virtual IoUringSocket& addClientSocket(os_fd_t fd, IoUringHandler& handler,
                                         uint32_t read_buffer_size) PURE;

  /**
   * Return the current thread's dispatcher.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * Submit a accept request for a socket.
   */
  virtual Request* submitAcceptRequest(IoUringSocket& socket, sockaddr_storage* remote_addr,
                                       socklen_t* remote_addr_len) PURE;

  /**
   * Submit a cancel request for a socket.
   */
  virtual Request* submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) PURE;

  /**
   * Submit a close request for a socket.
   */
  virtual Request* submitCloseRequest(IoUringSocket& socket) PURE;

  /**
   * Submit a read request for a socket.
   */
  virtual Request* submitReadRequest(IoUringSocket& socket, struct iovec* iov) PURE;

  /**
   * Submit a write request for a socket.
   */
  virtual Request* submitWritevRequest(IoUringSocket& socket, struct iovec* iovecs,
                                       uint64_t num_vecs) PURE;

  /**
   * Submit a connect request for a socket.
   */
  virtual Request*
  submitConnectRequest(IoUringSocket& socket,
                       const Network::Address::InstanceConstSharedPtr& address) PURE;
};

/**
 * Abstract factory for io_uring wrappers.
 */
class IoUringFactory {
public:
  virtual ~IoUringFactory() = default;

  /**
   * Returns the current thread's IoUringWorker. If it isn't register a worker yet,
   * absl::nullopt returned.
   */
  virtual OptRef<IoUringWorker> getIoUringWorker() PURE;

  /**
   * Initializes a factory upon server readiness. For example this method can be
   * used to set TLS.
   */
  virtual void onServerInitialized() PURE;

  /**
   * Indicates whether the current thread has IoUringWorker
   */
  virtual bool currentThreadRegistered() PURE;
};

} // namespace Io
} // namespace Envoy
