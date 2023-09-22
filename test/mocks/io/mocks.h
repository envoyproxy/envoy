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
  MOCK_METHOD(IoUringResult, submit, ());
  MOCK_METHOD(void, injectCompletion, (os_fd_t fd, Request* user_data, int32_t result));
  MOCK_METHOD(void, removeInjectedCompletion, (os_fd_t fd));
};

class MockIoUringSocket : public IoUringSocket {
public:
  MOCK_METHOD(IoUringWorker&, getIoUringWorker, (), (const));
  MOCK_METHOD(os_fd_t, fd, (), (const));
  MOCK_METHOD(void, onAccept, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onConnect, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onRead, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onWrite, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onClose, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onCancel, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, onShutdown, (Request * req, int32_t result, bool injected));
  MOCK_METHOD(void, injectCompletion, (Request::RequestType type));
};

} // namespace Io
} // namespace Envoy
