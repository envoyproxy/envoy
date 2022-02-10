#pragma once

#include "source/common/io/io_uring.h"

#include "liburing.h"

namespace Envoy {
namespace Io {

class IoUringFactoryImpl : public IoUringFactoryBase {
public:
  void initialize();

  // IoUringFactory
  IoUring& getOrCreate() const override;

  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override { return "envoy.extensions.io.io_uring"; };

private:
  uint32_t io_uring_size_{};
  bool use_submission_queue_polling_{};
  ThreadLocal::SlotPtr tls_;
};

class IoUringExtension : public Server::BootstrapExtension {
public:
  IoUringExtension(IoUringFactoryImpl& factory) : factory_(factory) {}

  // Server::BootstrapExtension
  void onServerInitialized() override { factory_.initialize(); }

protected:
  IoUringFactoryImpl& factory_;
};

class IoUringImpl : public IoUring, public ThreadLocal::ThreadLocalObject {
public:
  IoUringImpl(uint32_t io_uring_size, bool use_submission_queue_polling);
  ~IoUringImpl() override;

  os_fd_t registerEventfd() override;
  void unregisterEventfd() override;
  bool isEventfdRegistered() const override;
  void forEveryCompletion(CompletionCb completion_cb) override;
  IoUringResult prepareAccept(os_fd_t fd, struct sockaddr* remote_addr, socklen_t* remote_addr_len,
                              void* user_data) override;
  IoUringResult prepareConnect(os_fd_t fd, const Network::Address::InstanceConstSharedPtr& address,
                               void* user_data) override;
  IoUringResult prepareReadv(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs, off_t offset,
                             void* user_data) override;
  IoUringResult prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                              off_t offset, void* user_data) override;
  IoUringResult prepareClose(os_fd_t fd, void* user_data) override;
  IoUringResult submit() override;

private:
  const uint32_t io_uring_size_;
  struct io_uring ring_;
  std::vector<struct io_uring_cqe*> cqes_;
  os_fd_t event_fd_{INVALID_SOCKET};
};

DECLARE_FACTORY(IoUringFactoryImpl);

} // namespace Io
} // namespace Envoy
