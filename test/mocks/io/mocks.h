#pragma once

#include "envoy/common/io/io_uring.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Io {

class MockIoUring : public IoUring {
public:
  MOCK_METHOD(os_fd_t, registerEventfd, ());
  MOCK_METHOD(void, unregisterEventfd, ());
  MOCK_METHOD(bool, isEventfdRegistered, (), (const));
  MOCK_METHOD(void, forEveryCompletion, (const CompletionCb& completion_cb));
  MOCK_METHOD(IoUringResult, prepareAccept,
              (os_fd_t fd, struct sockaddr* remote_addr, socklen_t* remote_addr_len,
               Request* user_data));
  MOCK_METHOD(IoUringResult, prepareConnect,
              (os_fd_t fd, const Network::Address::InstanceConstSharedPtr& address,
               Request* user_data));
  MOCK_METHOD(IoUringResult, prepareReadv,
              (os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
               Request* user_data));
  MOCK_METHOD(IoUringResult, prepareWritev,
              (os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
               Request* user_data));
  MOCK_METHOD(IoUringResult, prepareClose, (os_fd_t fd, Request* user_data));
  MOCK_METHOD(IoUringResult, prepareCancel, (Request * cancelling_user_data, Request* user_data));
  MOCK_METHOD(IoUringResult, prepareShutdown, (os_fd_t fd, int how, Request* user_data));
  MOCK_METHOD(IoUringResult, prepareSend,
              (os_fd_t fd, const void* buf, size_t len, int flags, Request* user_data));
  MOCK_METHOD(IoUringResult, prepareRecv,
              (os_fd_t fd, void* buf, size_t len, int flags, Request* user_data));
  MOCK_METHOD(IoUringResult, prepareSendmsg,
              (os_fd_t fd, const struct msghdr* msg, int flags, Request* user_data));
  MOCK_METHOD(IoUringResult, prepareRecvmsg,
              (os_fd_t fd, struct msghdr* msg, int flags, Request* user_data));
  MOCK_METHOD(IoUringResult, submit, ());
  MOCK_METHOD(void, injectCompletion, (os_fd_t fd, Request* user_data, int32_t result));
  MOCK_METHOD(void, removeInjectedCompletion, (os_fd_t fd));
};

class MockIoUringSocket : public IoUringSocket {
public:
  MOCK_METHOD(os_fd_t, fd, (), (const));
  MOCK_METHOD(void, close, (bool, IoUringSocketOnClosedCb));
  MOCK_METHOD(void, shutdown, (int32_t how));
  MOCK_METHOD(void, enableRead, ());
  MOCK_METHOD(void, disableRead, ());
  MOCK_METHOD(void, enableCloseEvent, (bool enable));
  MOCK_METHOD(void, connect, (const Network::Address::InstanceConstSharedPtr& address));
  MOCK_METHOD(void, write, (Buffer::Instance & data));
  MOCK_METHOD(uint64_t, write, (const Buffer::RawSlice* slices, uint64_t num_slice));
  MOCK_METHOD(void, onAccept, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onConnect, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onRead, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onWrite, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onClose, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onCancel, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onShutdown, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onSend, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onRecv, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onSendmsg, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onRecvmsg, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, injectCompletion, (Request::RequestType type));
  MOCK_METHOD(IoUringSocketStatus, getStatus, (), (const));
  MOCK_METHOD(IoUringWorker&, getIoUringWorker, (), (const));
  MOCK_METHOD(const OptRef<ReadParam>&, getReadParam, (), (const));
  MOCK_METHOD(const OptRef<WriteParam>&, getWriteParam, (), (const));
  MOCK_METHOD(void, setFileReadyCb, (Event::FileReadyCb cb));
  MOCK_METHOD(Request*, submitSendRequest,
              (IoUringSocket & socket, const void* buf, size_t len, int flags));
  MOCK_METHOD(Request*, submitRecvRequest,
              (IoUringSocket & socket, void* buf, size_t len, int flags));
  MOCK_METHOD(Request*, submitSendmsgRequest,
              (IoUringSocket & socket, const struct msghdr* msg, int flags));
  MOCK_METHOD(Request*, submitRecvmsgRequest,
              (IoUringSocket & socket, struct msghdr* msg, int flags));
};

class MockIoUringWorker : public IoUringWorker {
public:
  MOCK_METHOD(IoUringSocket&, addServerSocket,
              (os_fd_t fd, Event::FileReadyCb cb, bool enable_close_event));
  MOCK_METHOD(IoUringSocket&, addServerSocket,
              (os_fd_t fd, Buffer::Instance& read_buf, Event::FileReadyCb cb,
               bool enable_close_event));
  MOCK_METHOD(IoUringSocket&, addClientSocket,
              (os_fd_t fd, Event::FileReadyCb cb, bool enable_close_event));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Request*, submitConnectRequest,
              (IoUringSocket & socket, const Network::Address::InstanceConstSharedPtr& address));
  MOCK_METHOD(Request*, submitReadRequest, (IoUringSocket & socket));
  MOCK_METHOD(Request*, submitWriteRequest,
              (IoUringSocket & socket, const Buffer::RawSliceVector& slices));
  MOCK_METHOD(Request*, submitCloseRequest, (IoUringSocket & socket));
  MOCK_METHOD(Request*, submitCancelRequest, (IoUringSocket & socket, Request* request_to_cancel));
  MOCK_METHOD(Request*, submitShutdownRequest, (IoUringSocket & socket, int how));
  MOCK_METHOD(Request*, submitSendRequest,
              (IoUringSocket & socket, const void* buf, size_t len, int flags));
  MOCK_METHOD(Request*, submitRecvRequest,
              (IoUringSocket & socket, void* buf, size_t len, int flags));
  MOCK_METHOD(Request*, submitSendmsgRequest,
              (IoUringSocket & socket, const struct msghdr* msg, int flags));
  MOCK_METHOD(Request*, submitRecvmsgRequest,
              (IoUringSocket & socket, struct msghdr* msg, int flags));
  MOCK_METHOD(uint32_t, getNumOfSockets, (), (const));
  MOCK_METHOD(IoUringMode, getMode, (), (const));
};

class MockIoUringWorkerFactory : public IoUringWorkerFactory {
public:
  MOCK_METHOD(OptRef<IoUringWorker>, getIoUringWorker, ());
  MOCK_METHOD(void, onWorkerThreadInitialized, ());
  MOCK_METHOD(bool, currentThreadRegistered, ());
};

} // namespace Io
} // namespace Envoy
