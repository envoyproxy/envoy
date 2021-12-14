#pragma once

#include "source/extensions/io_socket/io_uring/io_uring.h"

#include "liburing.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace IoUring {

class IoUringFactoryImpl : public IoUringFactory {
public:
  IoUringFactoryImpl(uint32_t io_uring_size, bool use_submission_queue_polling);
  IoUring& getOrCreateUring() const override;

private:
  const uint32_t io_uring_size_;
  const bool use_submission_queue_polling_;
};

class IoUringImpl : public IoUring {
public:
  IoUringImpl(uint32_t io_uring_size, bool use_submission_queue_polling);
  ~IoUringImpl() override;

  os_fd_t registerEventfd() override;
  void unregisterEventfd() override;
  bool isEventfdRegistered() const override;
  void forEveryCompletion(CompletionCb completion_cb) override;
  void prepareAccept(os_fd_t fd, struct sockaddr* remote_addr, socklen_t* remote_addr_len,
                     void* user_data) override;
  void prepareConnect(os_fd_t fd, const Network::Address::InstanceConstSharedPtr& address,
                      void* user_data) override;
  void prepareReadv(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
                    void* user_data) override;
  void prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
                     void* user_data) override;
  void prepareClose(os_fd_t fd, void* user_data) override;
  void submit() override;

private:
  struct io_uring_sqe* getSqe();

  const uint32_t io_uring_size_;
  struct io_uring ring_;
  std::vector<struct io_uring_cqe*> cqes_;
  os_fd_t event_fd_{INVALID_SOCKET};
};

} // namespace IoUring
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
