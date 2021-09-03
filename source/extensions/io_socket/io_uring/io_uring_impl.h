#pragma once

#include "source/extensions/io_socket/io_uring/io_uring.h"

#include "liburing.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace IoUring {

class IoUringFactoryImpl : public IoUringFactory {
public:
  IoUringFactoryImpl(const uint32_t io_uring_size, const bool use_submission_queue_polling);
  IoUring& getOrCreateUring() const override;

private:
  const uint32_t io_uring_size_;
  const bool use_submission_queue_polling_;
};

class IoUringImpl : public IoUring {
public:
  IoUringImpl(const uint32_t io_uring_size, const bool use_submission_queue_polling);
  ~IoUringImpl() override;

  os_fd_t registerEventfd() override;
  void unregisterEventfd() override;
  bool isEventfdRegistered() const override;
  void forEveryCompletion(std::function<void(Request&, int32_t)> completion_cb) override;
  void prepareAccept(os_fd_t fd, struct sockaddr* remote_addr, socklen_t* remote_addr_len) override;
  void prepareConnect(os_fd_t fd, IoUringSocketHandleImpl& iohandle,
                      const Network::Address::InstanceConstSharedPtr& address) override;
  void prepareRead(os_fd_t fd, IoUringSocketHandleImpl& iohandle, struct iovec* iov) override;
  void prepareWrite(os_fd_t fd, std::list<Buffer::SliceDataPtr>&& slices) override;
  void prepareClose(os_fd_t fd) override;
  void submit() override;

private:
  struct io_uring_sqe* getSqe();

  const uint32_t io_uring_size_;
  struct io_uring ring_;
  std::vector<struct io_uring_cqe*> cqes_;
  bool is_eventfd_registered_{false};
};

} // namespace IoUring
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
