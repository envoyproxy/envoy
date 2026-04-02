#pragma once

#include <functional>

#include "envoy/common/pure.h"
#include "envoy/network/address.h"
#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace Io {

class IoUringSocket;

/**
 * Abstract for io_uring I/O Request.
 */
class Request {
public:
  /**
   * io_uring request type.
   */
  enum class RequestType : uint8_t {
    Accept = 0x1,
    Connect = 0x2,
    Read = 0x4,
    Write = 0x8,
    Close = 0x10,
    Cancel = 0x20,
    Shutdown = 0x40,
  };

  Request(RequestType type, IoUringSocket& socket) : type_(type), socket_(socket) {}
  virtual ~Request() = default;

  /**
   * Return the request type.
   */
  RequestType type() const { return type_; }

  /**
   * Returns the io_uring socket the request belongs to.
   */
  IoUringSocket& socket() const { return socket_; }

private:
  RequestType type_;
  IoUringSocket& socket_;
};

/**
 * Callback invoked when iterating over entries in the completion queue.
 * @param user_data is any data attached to an entry submitted to the submission queue.
 * @param result is a return code of the submitted system call.
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
 * Abstract wrapper around io_uring.
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
   * Iterates over entries in the completion queue, calls the given callback for every entry
   * and marks them consumed.
   */
  virtual void forEveryCompletion(const CompletionCb& completion_cb) PURE;

  /**
   * Prepares an accept and puts it into the submission queue.
   * @return IoUringResult::Failed if the submission queue is full, IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareAccept(os_fd_t fd, struct sockaddr* remote_addr,
                                      socklen_t* remote_addr_len, Request* user_data) PURE;

  /**
   * Prepares a connect and puts it into the submission queue.
   * @return IoUringResult::Failed if the submission queue is full, IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareConnect(os_fd_t fd,
                                       const Network::Address::InstanceConstSharedPtr& address,
                                       Request* user_data) PURE;

  /**
   * Prepares a readv and puts it into the submission queue.
   * @return IoUringResult::Failed if the submission queue is full, IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareReadv(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                                     off_t offset, Request* user_data) PURE;

  /**
   * Prepares a writev and puts it into the submission queue.
   * @return IoUringResult::Failed if the submission queue is full, IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                                      off_t offset, Request* user_data) PURE;

  /**
   * Prepares a socket recv and puts it into the submission queue. Preferred over readv for
   * socket operations as it supports socket-specific flags.
   * @return IoUringResult::Failed if the submission queue is full, IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareRecv(os_fd_t fd, void* buf, uint32_t len, int flags,
                                    Request* user_data) PURE;

  /**
   * Prepares a socket send and puts it into the submission queue. Preferred over writev for
   * socket operations as it supports socket-specific flags and adds MSG_NOSIGNAL.
   * @return IoUringResult::Failed if the submission queue is full, IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareSend(os_fd_t fd, const void* buf, uint32_t len, int flags,
                                    Request* user_data) PURE;

  /**
   * Prepares a close and puts it into the submission queue.
   * @return IoUringResult::Failed if the submission queue is full, IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareClose(os_fd_t fd, Request* user_data) PURE;

  /**
   * Prepares a cancellation and puts it into the submission queue.
   * @return IoUringResult::Failed if the submission queue is full, IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareCancel(Request* cancelling_user_data, Request* user_data) PURE;

  /**
   * Prepares a shutdown and puts it into the submission queue.
   * @return IoUringResult::Failed if the submission queue is full, IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareShutdown(os_fd_t fd, int how, Request* user_data) PURE;

  /**
   * Submits the entries in the submission queue to the kernel using the
   * io_uring_enter() system call.
   * @return IoUringResult::Ok on success, IoUringResult::Busy if the CQ is overcommitted.
   */
  virtual IoUringResult submit() PURE;

  /**
   * Injects a request completion into the io_uring. Injected completions are iterated during
   * forEveryCompletion() to emulate read/write/close readiness from the IoSocketHandle layer.
   * @param fd the file descriptor this completion refers to.
   * @param user_data the user data related to this completion.
   * @param result the request result for this completion.
   */
  virtual void injectCompletion(os_fd_t fd, Request* user_data, int32_t result) PURE;

  /**
   * Removes all injected completions for the given file descriptor. Called during socket cleanup.
   * @param fd the file descriptor whose injected completions will be removed.
   */
  virtual void removeInjectedCompletion(os_fd_t fd) PURE;

  /**
   * Returns the number of times the CQ ring has overflowed.
   */
  virtual uint64_t cqOverflowCount() const PURE;
};

using IoUringPtr = std::unique_ptr<IoUring>;
class IoUringWorker;

/**
 * The status of an IoUringSocket.
 */
enum IoUringSocketStatus {
  Initialized,
  ReadEnabled,
  ReadDisabled,
  RemoteClosed,
  Closed,
};

/**
 * Callback invoked when a close operation completes on a socket.
 */
using IoUringSocketOnClosedCb = std::function<void(Buffer::Instance& read_buffer)>;

/**
 * The data returned from a read request.
 */
struct ReadParam {
  Buffer::Instance& buf_;
  int32_t result_;
};

/**
 * The data returned from a write request.
 */
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
   * Get the IoUringWorker this socket is bound to.
   */
  virtual IoUringWorker& getIoUringWorker() const PURE;

  /**
   * Return the raw fd.
   */
  virtual os_fd_t fd() const PURE;

  /**
   * Close the socket.
   * @param keep_fd_open if true, the IoUringSocket is destroyed but the fd remains open. Used
   *   for migrating sockets between worker threads.
   * @param cb invoked when the close request completes. Also used for migration.
   */
  virtual void close(bool keep_fd_open, IoUringSocketOnClosedCb cb = nullptr) PURE;

  /**
   * Enable read on the socket. Begins submitting read requests and delivering read events.
   */
  virtual void enableRead() PURE;

  /**
   * Disable read on the socket. Stops submitting new read requests; existing requests are
   * not canceled.
   */
  virtual void disableRead() PURE;

  /**
   * Enable or disable close event delivery. When enabled, a remote close detected by a read
   * is delivered as a FileReadyType::Closed event.
   */
  virtual void enableCloseEvent(bool enable) PURE;

  /**
   * Connect to an address.
   * @param address the peer address to connect to.
   */
  virtual void connect(const Network::Address::InstanceConstSharedPtr& address) PURE;

  /**
   * Write data to the socket.
   * @param data the buffer to write.
   */
  virtual void write(Buffer::Instance& data) PURE;

  /**
   * Write data to the socket.
   * @param slices the data slices to write.
   * @param num_slice the number of slices.
   */
  virtual uint64_t write(const Buffer::RawSlice* slices, uint64_t num_slice) PURE;

  /**
   * Shutdown the socket.
   * @param how one of SHUT_RD, SHUT_WR, or SHUT_RDWR.
   */
  virtual void shutdown(int how) PURE;

  /**
   * On accept request completed.
   * @param req the request object used as user data.
   * @param result the kernel result of the operation.
   * @param injected indicates if the completion was injected.
   */
  virtual void onAccept(Request* req, int32_t result, bool injected) PURE;

  /**
   * On connect request completed.
   * @param req the request object used as user data.
   * @param result the kernel result of the operation.
   * @param injected indicates if the completion was injected.
   */
  virtual void onConnect(Request* req, int32_t result, bool injected) PURE;

  /**
   * On read request completed.
   * @param req the ReadRequest object used as user data.
   * @param result the kernel result (bytes read or negative errno).
   * @param injected indicates if the completion was injected.
   */
  virtual void onRead(Request* req, int32_t result, bool injected) PURE;

  /**
   * On write request completed.
   * @param req the WriteRequest object used as user data.
   * @param result the kernel result (bytes written or negative errno).
   * @param injected indicates if the completion was injected.
   */
  virtual void onWrite(Request* req, int32_t result, bool injected) PURE;

  /**
   * On close request completed.
   * @param req the request object used as user data.
   * @param result the kernel result of the operation.
   * @param injected indicates if the completion was injected.
   */
  virtual void onClose(Request* req, int32_t result, bool injected) PURE;

  /**
   * On cancel request completed.
   * @param req the request object used as user data.
   * @param result the kernel result of the operation.
   * @param injected indicates if the completion was injected.
   */
  virtual void onCancel(Request* req, int32_t result, bool injected) PURE;

  /**
   * On shutdown request completed.
   * @param req the request object used as user data.
   * @param result the kernel result of the operation.
   * @param injected indicates if the completion was injected.
   */
  virtual void onShutdown(Request* req, int32_t result, bool injected) PURE;

  /**
   * Inject a request completion to the io_uring instance.
   * @param type the request type of the injected completion.
   */
  virtual void injectCompletion(Request::RequestType type) PURE;

  /**
   * Return the current status of the IoUringSocket.
   */
  virtual IoUringSocketStatus getStatus() const PURE;

  /**
   * Return the data from the read request.
   * @return valid ReadParam when the callback is invoked with Event::FileReadyType::Read,
   *   otherwise absl::nullopt.
   */
  virtual const OptRef<ReadParam>& getReadParam() const PURE;

  /**
   * Return the data from the write request.
   * @return valid WriteParam when the callback is invoked with Event::FileReadyType::Write,
   *   otherwise absl::nullopt.
   */
  virtual const OptRef<WriteParam>& getWriteParam() const PURE;

  /**
   * Set the callback for file ready events.
   * @param cb the callback function.
   */
  virtual void setFileReadyCb(Event::FileReadyCb cb) PURE;
};

using IoUringSocketPtr = std::unique_ptr<IoUringSocket>;

/**
 * Abstract for per-thread worker.
 */
class IoUringWorker : public ThreadLocal::ThreadLocalObject {
public:
  ~IoUringWorker() override = default;

  /**
   * Add a server socket to the worker.
   */
  virtual IoUringSocket& addServerSocket(os_fd_t fd, Event::FileReadyCb cb,
                                         bool enable_close_event) PURE;

  /**
   * Add a server socket from an existing socket from another thread.
   */
  virtual IoUringSocket& addServerSocket(os_fd_t fd, Buffer::Instance& read_buf,
                                         Event::FileReadyCb cb, bool enable_close_event) PURE;

  /**
   * Add a client socket to the worker.
   */
  virtual IoUringSocket& addClientSocket(os_fd_t fd, Event::FileReadyCb cb,
                                         bool enable_close_event) PURE;

  /**
   * Return the current thread's dispatcher.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * Submit a connect request for a socket.
   */
  virtual Request*
  submitConnectRequest(IoUringSocket& socket,
                       const Network::Address::InstanceConstSharedPtr& address) PURE;

  /**
   * Submit a read request for a socket.
   */
  virtual Request* submitReadRequest(IoUringSocket& socket) PURE;

  /**
   * Submit a write request for a socket.
   */
  virtual Request* submitWriteRequest(IoUringSocket& socket,
                                      const Buffer::RawSliceVector& slices) PURE;

  /**
   * Submit a close request for a socket.
   */
  virtual Request* submitCloseRequest(IoUringSocket& socket) PURE;

  /**
   * Submit a cancel request for a socket.
   */
  virtual Request* submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) PURE;

  /**
   * Submit a shutdown request for a socket.
   */
  virtual Request* submitShutdownRequest(IoUringSocket& socket, int how) PURE;

  /**
   * Return the number of sockets in the worker.
   */
  virtual uint32_t getNumOfSockets() const PURE;
};

/**
 * Abstract factory for IoUringWorker wrappers.
 */
class IoUringWorkerFactory {
public:
  virtual ~IoUringWorkerFactory() = default;

  /**
   * Returns the current thread's IoUringWorker, or absl::nullopt if not registered.
   */
  virtual OptRef<IoUringWorker> getIoUringWorker() PURE;

  /**
   * Initializes an IoUringWorkerFactory upon server readiness. Sets the TLS slot.
   */
  virtual void onWorkerThreadInitialized() PURE;

  /**
   * Returns true if the current thread has a registered IoUringWorker.
   */
  virtual bool currentThreadRegistered() PURE;
};

} // namespace Io
} // namespace Envoy
