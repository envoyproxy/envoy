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
   * Submits the entries in the submission queue to the kernel using the
   * `io_uring_enter()` system call.
   * Returns IoUringResult::Ok in case of success and may return
   * IoUringResult::Busy if we over commit the number of requests. In the latter
   * case the application should drain the completion queue by handling some completions
   * with the forEveryCompletion() method and try again.
   */
  virtual IoUringResult submit() PURE;
};

/**
 * IoUring request type.
 */
enum class RequestType { Accept, Connect, Read, Write, Close, Cancel };

class IoUringSocket;

/**
 * Abstract for IoUring I/O Request.
 */
struct Request {
  RequestType type_;
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
  ~IoUringSocket() override = default;

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
  virtual uint64_t write(Buffer::Instance&) PURE;
  virtual uint64_t writev(const Buffer::RawSlice*, uint64_t) PURE;

  /**
   * Connect to an address.
   */
  virtual void connect(const Network::Address::InstanceConstSharedPtr&) PURE;

  /**
   * On accept request completed.
   */
  virtual void onAccept(int32_t);

  /**
   * On close request completed.
   */
  virtual void onClose(int32_t);
  virtual void onCancel(int32_t);
  virtual void onConnect(int32_t);
  virtual void onRead(int32_t);
  virtual void onWrite(int32_t);
};

/**
 * The handler for IoUring request completion.
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

  /**
   * Remove the socket from the worker.
   */
  virtual std::unique_ptr<IoUringSocket> removeSocket(IoUringSocket& socket) PURE;
};

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
