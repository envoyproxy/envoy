#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/network/address.h"
#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace Io {

class IoUringSocket;

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
  static constexpr uint32_t Shutdown = 0x40;
};

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
 * @param user_data is any data attached to an entry submitted to the submission
 * queue.
 * @param result is a return code of submitted system call.
 * @param injected indicates whether the completion is injected or not.
 */
using CompletionCb = std::function<void(Request* user_data, int32_t result, bool injected)>;

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
   * Prepares a cancellation and puts it into the submission queue.
   * Returns IoUringResult::Failed in case the submission queue is full already
   * and IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareCancel(Request* cancelling_user_data, Request* user_data) PURE;

  /**
   * Prepares a shutdown operation and puts it into the submission queue.
   * Returns IoUringResult::Failed in case the submission queue is full already
   * and IoUringResult::Ok otherwise.
   */
  virtual IoUringResult prepareShutdown(os_fd_t fd, int how, Request* user_data) PURE;

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
class IoUringWorker;

/**
 * The Status of IoUringSocket.
 */
enum IoUringSocketStatus {
  Initialized,
  Enabled,
  Disabled,
  RemoteClosed,
  Closed,
};

using IoUringSocketOnClosedCb = std::function<void()>;

struct AcceptedSocketParam {
  os_fd_t fd_;
  sockaddr_storage* remote_addr_;
  socklen_t remote_addr_len_;
};

struct ReadParam {
  Buffer::Instance& buf_;
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
   * Get the IoUringWorker this socket bind to.
   */
  virtual IoUringWorker& getIoUringWorker() const PURE;

  /**
   * Return the raw fd.
   */
  virtual os_fd_t fd() const PURE;

  /**
   * Close the socket.
   * param keep_fd_open is indicated the file descriptor of the socket will be closed or not in the
   * end. The value of `true` is used for destroy the IoUringSocket but keep the file descriptor
   * open.
   */
  virtual void close(bool keep_fd_open, IoUringSocketOnClosedCb cb = nullptr) PURE;

  /**
   * Enable the socket.
   */
  virtual void enable() PURE;

  /**
   * Disable the socket.
   */
  virtual void disable() PURE;

  /**
   * Enable close event.
   */
  virtual void enableCloseEvent(bool enable) PURE;

  /**
   * Connect to an address.
   * @param address the peer of address which is connected to.
   */
  virtual void connect(const Network::Address::InstanceConstSharedPtr& address) PURE;

  /**
   * Write data to the socket.
   */
  virtual void write(Buffer::Instance& data) PURE;

  /**
   * Write data to the socket.
   */
  virtual uint64_t write(const Buffer::RawSlice* slices, uint64_t num_slice) PURE;

  /**
   * Shutdown the socket.
   * @param how is SHUT_RD, SHUT_WR and SHUT_RDWR.
   */
  virtual void shutdown(int how) PURE;

  /**
   * On accept request completed.
   * TODO (soulxu): wrap the raw result into a type. It can be `IoCallUint64Result`.
   * @param req the AcceptRequest object which is as request user data.
   * @param result the result of operation in the request.
   * @param injected indicates the completion is injected or not.
   */
  virtual void onAccept(Request* req, int32_t result, bool injected) PURE;

  /**
   * On connect request completed.
   * TODO (soulxu): wrap the raw result into a type. It can be `IoCallUint64Result`.
   * @param req the request object which is as request user data.
   * @param result the result of operation in the request.
   * @param injected indicates the completion is injected or not.
   */
  virtual void onConnect(Request* req, int32_t result, bool injected) PURE;

  /**
   * On read request completed.
   * TODO (soulxu): wrap the raw result into a type. It can be `IoCallUint64Result`.
   * @param req the ReadRequest object which is as request user data.
   * @param result the result of operation in the request.
   * @param injected indicates the completion is injected or not.
   */
  virtual void onRead(Request* req, int32_t result, bool injected) PURE;

  /**
   * On write request completed.
   * TODO (soulxu): wrap the raw result into a type. It can be `IoCallUint64Result`.
   * @param req the WriteRequest object which is as request user data.
   * @param result the result of operation in the request.
   * @param injected indicates the completion is injected or not.
   */
  virtual void onWrite(Request* req, int32_t result, bool injected) PURE;

  /**
   * On close request completed.
   * TODO (soulxu): wrap the raw result into a type. It can be `IoCallUint64Result`.
   * @param req the request object which is as request user data.
   * @param result the result of operation in the request.
   * @param injected indicates the completion is injected or not.
   */
  virtual void onClose(Request* req, int32_t result, bool injected) PURE;

  /**
   * On cancel request completed.
   * TODO (soulxu): wrap the raw result into a type. It can be `IoCallUint64Result`.
   * @param req the request object which is as request user data.
   * @param result the result of operation in the request.
   * @param injected indicates the completion is injected or not.
   */
  virtual void onCancel(Request* req, int32_t result, bool injected) PURE;

  /**
   * On shutdown request completed.
   * TODO (soulxu): wrap the raw result into a type. It can be `IoCallUint64Result`.
   * @param req the request object which is as request user data.
   * @param result the result of operation in the request.
   * @param injected indicates the completion is injected or not.
   */
  virtual void onShutdown(Request* req, int32_t result, bool injected) PURE;

  /**
   * Inject a request completion to the io uring instance.
   * @param type the request type of injected completion.
   */
  virtual void injectCompletion(Request::RequestType type) PURE;

  /**
   * Return the current status of IoUringSocket.
   * @return the status.
   */
  virtual IoUringSocketStatus getStatus() const PURE;

  virtual const OptRef<ReadParam>& getReadParam() const PURE;
  virtual const OptRef<AcceptedSocketParam>& getAcceptedSocketParam() const PURE;
  virtual const OptRef<WriteParam>& getWriteParam() const PURE;

  virtual void clearAcceptedSocketParam() PURE;
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
   * Add an accept socket socket to the worker.
   */
  virtual IoUringSocket& addAcceptSocket(os_fd_t fd, Event::FileReadyCb cb,
                                         bool enable_close_event) PURE;

  /**
   * Add an server socket socket to the worker.
   */
  virtual IoUringSocket& addServerSocket(os_fd_t fd, Event::FileReadyCb cb,
                                         bool enable_close_event) PURE;

  /**
   * Add an server socket through an existing socket from another thread.
   */
  virtual IoUringSocket& addServerSocket(os_fd_t fd, Buffer::Instance& read_buf,
                                         Event::FileReadyCb cb, bool enable_close_event) PURE;
  /**
   * Add an client socket socket to the worker.
   */
  virtual IoUringSocket& addClientSocket(os_fd_t fd, Event::FileReadyCb cb,
                                         bool enable_close_event) PURE;

  /**
   * Return the current thread's dispatcher.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * Submit a accept request for a socket.
   */
  virtual Request* submitAcceptRequest(IoUringSocket& socket) PURE;

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
  virtual void onWorkerThreadInitialized() PURE;

  /**
   * Indicates whether the current thread has IoUringWorker
   */
  virtual bool currentThreadRegistered() PURE;
};

} // namespace Io
} // namespace Envoy
