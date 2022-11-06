#pragma once

#include "source/common/io/io_uring.h"

#include "liburing.h"

namespace Envoy {
namespace Io {

bool isIoUringSupported();

struct InjectedCompletion {
  InjectedCompletion(void* user_data, int32_t result) : user_data_(user_data), result_(result) {}
  void* user_data_;
  int32_t result_;
};

class IoUringImpl : public IoUring {
public:
  IoUringImpl(uint32_t io_uring_size, bool use_submission_queue_polling);
  ~IoUringImpl() override;

  os_fd_t registerEventfd() override;
  void unregisterEventfd() override;
  bool isEventfdRegistered() const override;
  void forEveryCompletion(CompletionCb completion_cb) override;
  void injectCompletion(void* user_data, int32_t result) override;

  IoUringResult prepareAccept(os_fd_t fd, struct sockaddr* remote_addr, socklen_t* remote_addr_len,
                              void* user_data) override;
  IoUringResult prepareConnect(os_fd_t fd, const Network::Address::InstanceConstSharedPtr& address,
                               void* user_data) override;
  IoUringResult prepareReadv(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
                             void* user_data) override;
  IoUringResult prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                              off_t offset, void* user_data) override;
  IoUringResult prepareClose(os_fd_t fd, void* user_data) override;
  IoUringResult prepareCancel(void* cancelling_user_data, void* user_data) override;
  IoUringResult submit() override;

private:
  const uint32_t io_uring_size_;
  struct io_uring ring_ {};
  std::vector<struct io_uring_cqe*> cqes_;
  os_fd_t event_fd_{INVALID_SOCKET};
  std::vector<InjectedCompletion> injected_completions_;
};

} // namespace Io
} // namespace Envoy
