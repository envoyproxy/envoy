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
  ReadEnabled,
  ReadDisabled,
  RemoteClosed,
  Closed,
};

/**
 * A callback will be invoked when a close requested done on the socket.
 */
using IoUringSocketOnClosedCb = std::function<void(Buffer::Instance& read_buffer)>;

/**
 * The data returned from the read request.
 */
struct ReadParam {
  Buffer::Instance& buf_;
  int32_t result_;
};

/**
 * The data returned from the write request.
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
   * Get the IoUringWorker this socket bind to.
   */
  virtual IoUringWorker& getIoUringWorker() const PURE;

  /**
   * Return the raw fd.
   */
  virtual os_fd_t fd() const PURE;

  /**
   * Close the socket.
   * @param keep_fd_open indicates the file descriptor of the socket will be closed or not in the
   * end. The value of `true` is used for destroy the IoUringSocket but keep the file descriptor
   * open. This is used for migrating the IoUringSocket between worker threads.
   * @param cb will be invoked when the close request is done. This is also used for migrating the
   * IoUringSocket between worker threads.
   */
  virtual void close(bool keep_fd_open, IoUringSocketOnClosedCb cb = nullptr) PURE;

  /**
   * Enable the read on the socket. The socket will be begin to submit the read request and deliver
   * read event when the request is done. This is used when the socket is listening on the file read
   * event.
   */
  virtual void enableRead() PURE;

  /**
   * Disable the read on the socket. The socket stops to submit the read request, although the
   * existing read request won't be canceled and no read event will be delivered. This is used when
   * the socket isn't listening on the file read event.
   */
  virtual void disableRead() PURE;

  /**
   * Enable close event. This is used for the case the socket is listening on the file close event.
   * Then a remote close is found by a read request will be delivered as a file close event.
   */
  virtual void enableCloseEvent(bool enable) PURE;

  /**
   * Connect to an address.
   * @param address the peer of address which is connected to.
   */
  virtual void connect(const Network::Address::InstanceConstSharedPtr& address) PURE;

  /**
   * Write data to the socket.
   * @param data is going to write.
   */
  virtual void write(Buffer::Instance& data) PURE;

  /**
   * Write data to the socket.
   * @param slices includes the data to write.
   * @param num_slice the number of slices.
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

  /**
   * Return the data get from the read request.
   * @return Only return valid ReadParam when the callback is invoked with
   * `Event::FileReadyType::Read`, otherwise `absl::nullopt` returned.
   */
  virtual const OptRef<ReadParam>& getReadParam() const PURE;
  /**
   * Return the data get from the write request.
   * @return Only return valid WriteParam when the callback is invoked with
   * `Event::FileReadyType::Write`, otherwise `absl::nullopt` returned.
   */
  virtual const OptRef<WriteParam>& getWriteParam() const PURE;

  /**
   * Set the callback when file ready event triggered.
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
   * Returns the current thread's IoUringWorker. If the thread have not registered a IoUringWorker,
   * an absl::nullopt will be returned.
   */
  virtual OptRef<IoUringWorker> getIoUringWorker() PURE;

  /**
   * Initializes a IoUringWorkerFactory upon server readiness. The method is used to set the TLS.
   */
  virtual void onWorkerThreadInitialized() PURE;

  /**
   * Indicates whether the current thread has been registered for a IoUringWorker.
   */
  virtual bool currentThreadRegistered() PURE;
};

} // namespace Io
} // namespace Envoy
