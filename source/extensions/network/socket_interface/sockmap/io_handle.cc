#include "source/extensions/network/socket_interface/sockmap/io_handle.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/utility.h"

namespace Envoy {
namespace Network {

Api::IoCallUint64Result SockmapIoSocketHandle::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                                     uint64_t num_slice) {
  Api::IoCallUint64Result result = IoSocketHandleImpl::readv(max_length, slices, num_slice);
  maybeRegister(result);
  return result;
}

Api::IoCallUint64Result SockmapIoSocketHandle::writev(const Buffer::RawSlice* slices,
                                                      uint64_t num_slice) {
  Api::IoCallUint64Result result = IoSocketHandleImpl::writev(slices, num_slice);
  maybeRegister(result);
  return result;
}

IoHandlePtr SockmapIoSocketHandle::accept(struct sockaddr* addr, socklen_t* addrlen) {
  Api::SysCallSocketResult result = Api::OsSysCallsSingleton::get().accept(fd_, addr, addrlen);
  if (SOCKET_INVALID(result.return_value_)) {
    return nullptr;
  }
  // Accepted connections also register from user space so both ends of a same-host hop are present.
  // io_uring is never active under the sockmap interface, so the base platform handle is not used.
  return std::make_unique<SockmapIoSocketHandle>(datapath_, result.return_value_, socket_v6only_,
                                                 domain_);
}

IoHandlePtr SockmapIoSocketHandle::duplicate() {
  Api::SysCallSocketResult result = Api::OsSysCallsSingleton::get().duplicate(fd_);
  RELEASE_ASSERT(result.return_value_ != -1,
                 fmt::format("duplicate failed for '{}': ({}) {}", fd_, result.errno_,
                             errorDetails(result.errno_)));
  // The duplicate keeps the accelerated handle so accepted connections on cloned listeners are
  // registered too. It starts unregistered and registers on its own first transfer, so a duplicated
  // connection socket and the original manage their shared tuple key independently.
  return std::make_unique<SockmapIoSocketHandle>(datapath_, result.return_value_, socket_v6only_,
                                                 domain_, addressCacheMaxSize());
}

SockmapIoSocketHandle::~SockmapIoSocketHandle() { removeRegistration(); }

Api::IoCallUint64Result SockmapIoSocketHandle::close() {
  removeRegistration();
  return IoSocketHandleImpl::close();
}

void SockmapIoSocketHandle::removeRegistration() {
  // Remove the tuple key so a later connection that reuses the tuple is not redirected into a stale
  // entry. The captured addresses keep this independent of the fd state, and clearing them means
  // close and the destructor remove the key at most once.
  if (registered_local_ != nullptr && registered_peer_ != nullptr) {
    datapath_->unregisterSocket(*registered_local_, *registered_peer_);
    registered_local_.reset();
    registered_peer_.reset();
  }
}

void SockmapIoSocketHandle::registerOnFirstTransfer(const Api::IoCallUint64Result& result) {
  if (!result.ok() || result.return_value_ == 0) {
    return;
  }
  // A successful transfer means the connection is established, so the tuple is now resolvable.
  registered_ = true;
  const absl::StatusOr<Address::InstanceConstSharedPtr> local = localAddress();
  const absl::StatusOr<Address::InstanceConstSharedPtr> peer = peerAddress();
  if (local.ok() && peer.ok()) {
    datapath_->registerSocket(fd_, *local.value(), *peer.value());
    // Capture the addresses so close can remove the same key.
    registered_local_ = local.value();
    registered_peer_ = peer.value();
  }
}

} // namespace Network
} // namespace Envoy
