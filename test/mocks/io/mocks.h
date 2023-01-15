#include "envoy/common/io/io_uring.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Io {

class MockIoUring : public IoUring {
public:
  MOCK_METHOD(os_fd_t, registerEventfd, ());
  MOCK_METHOD(void, unregisterEventfd, ());
  MOCK_METHOD(bool, isEventfdRegistered, (), (const));
  MOCK_METHOD(void, forEveryCompletion, (CompletionCb completion_cb));
  MOCK_METHOD(IoUringResult, prepareAccept,
              (os_fd_t fd, struct sockaddr* remote_addr, socklen_t* remote_addr_len,
               void* user_data));
  MOCK_METHOD(IoUringResult, prepareConnect,
              (os_fd_t fd, const Network::Address::InstanceConstSharedPtr& address,
               void* user_data));
  MOCK_METHOD(IoUringResult, prepareReadv,
              (os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
               void* user_data));
  MOCK_METHOD(IoUringResult, prepareWritev,
              (os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
               void* user_data));
  MOCK_METHOD(IoUringResult, prepareClose, (os_fd_t fd, void* user_data));
  MOCK_METHOD(IoUringResult, prepareCancel, (void* cancelling_user_data, void* user_data));
  MOCK_METHOD(IoUringResult, submit, ());
  MOCK_METHOD(void, injectCompletion, (os_fd_t fd, void* user_data, int32_t result));
  MOCK_METHOD(void, removeInjectedCompletion, (os_fd_t fd));
};

} // namespace Io
} // namespace Envoy
