#pragma once

#include "envoy/thread_local/thread_local.h"

#include "source/common/io/io_uring.h"

#include "liburing.h"

namespace Envoy {
namespace Io {

bool isIoUringSupported();

class IoUringImpl : public IoUring, public ThreadLocal::ThreadLocalObject {
public:
  IoUringImpl(uint32_t io_uring_size, bool use_submission_queue_polling);
  ~IoUringImpl() override;

  void registerEvent(Event::Dispatcher& dispatcher, Event::FileTriggerType trigger) override;
  void unregisterEvent() override;
  bool isEventRegistered() const override;
  IoUringResult prepareAccept(os_fd_t fd, struct sockaddr* remote_addr, socklen_t* remote_addr_len,
                              void* user_data, CompletionCb cb) override;
  IoUringResult prepareConnect(os_fd_t fd, const Network::Address::InstanceConstSharedPtr& address,
                               void* user_data, CompletionCb cb) override;
  IoUringResult prepareReadv(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
                             void* user_data, CompletionCb cb) override;
  IoUringResult prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                              off_t offset, void* user_data, CompletionCb cb) override;
  IoUringResult prepareClose(os_fd_t fd, void* user_data, CompletionCb cb) override;
  IoUringResult prepareCancel(void* cancelling_user_data, void* user_data,
                              CompletionCb cb) override;
  IoUringResult prepareNop(void* user_data, CompletionCb cb) override;
  IoUringResult submit() override;
  IoUringResult trySubmit() override;

private:
  const uint32_t io_uring_size_;
  struct io_uring ring_ {};
  std::vector<struct io_uring_cqe*> cqes_;
  os_fd_t event_fd_{INVALID_SOCKET};
  Event::FileEventPtr file_event_{nullptr};
  absl::flat_hash_map<uint64_t, CompletionCb> cbs_;
  bool delay_submit_{false};

  void onFileEvent();
};

class IoUringFactoryImpl : public IoUringFactory {
public:
  IoUringFactoryImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                     ThreadLocal::SlotAllocator& tls);

  // IoUringFactory
  // TODO (soulxu): rename this method, it only about `get`.
  OptRef<IoUring> get() const override;
  void onServerInitialized() override;
  bool currentThreadRegistered() override;

private:
  const uint32_t io_uring_size_{};
  const bool use_submission_queue_polling_{};
  ThreadLocal::TypedSlot<IoUringImpl> tls_;
};

} // namespace Io
} // namespace Envoy
