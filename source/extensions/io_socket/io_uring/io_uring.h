#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

#include "source/common/network/address_impl.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace IoUring {

enum class RequestType { Accept, Connect, Read, Write, Close, Unknown };

class IoUringSocketHandleImpl;

using IoUringSocketHandleImplOptRef =
    absl::optional<std::reference_wrapper<IoUringSocketHandleImpl>>;

struct Request {
  IoUringSocketHandleImplOptRef iohandle_{absl::nullopt};
  RequestType type_{RequestType::Unknown};
  struct iovec* iov_{nullptr};
  std::list<Buffer::SliceDataPtr> slices_{};
};

class IoUring {
public:
  virtual ~IoUring() = default;

  virtual os_fd_t registerEventfd() PURE;
  virtual void unregisterEventfd() PURE;
  virtual bool isEventfdRegistered() const PURE;
  virtual void forEveryCompletion(std::function<void(Request&, int32_t)> completion_cb) PURE;
  virtual void prepareAccept(os_fd_t fd, struct sockaddr* remote_addr,
                             socklen_t* remote_addr_len) PURE;
  virtual void prepareConnect(os_fd_t fd, IoUringSocketHandleImpl& iohandle,
                              const Network::Address::InstanceConstSharedPtr& address) PURE;
  virtual void prepareRead(os_fd_t fd, IoUringSocketHandleImpl& iohandle, struct iovec* iov) PURE;
  virtual void prepareWrite(os_fd_t fd, std::list<Buffer::SliceDataPtr>&& slices) PURE;
  virtual void prepareClose(os_fd_t fd) PURE;
  virtual void submit() PURE;
};

class IoUringFactory {
public:
  virtual ~IoUringFactory() = default;

  virtual IoUring& getOrCreateUring() const PURE;
};

} // namespace IoUring
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
