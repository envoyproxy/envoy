#pragma once

#include <optional>

#include "source/common/common/assert.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/extensions/network/socket_interface/sockmap/bpf_datapath.h"

namespace Envoy {
namespace Network {

// IoSocketHandleImpl variant that registers its socket into the sockhash so the `sk_msg` program
// can redirect same-host payloads. Registration happens on the first successful read or write, when
// the connection is established, and is a single guarded attempt that keeps the hot path free of
// work.
class SockmapIoSocketHandle : public IoSocketHandleImpl {
public:
  SockmapIoSocketHandle(BpfDatapathSharedPtr datapath, os_fd_t fd, bool socket_v6only = false,
                        std::optional<int> domain = std::nullopt,
                        size_t address_cache_max_capacity = 0)
      : IoSocketHandleImpl(fd, socket_v6only, domain, address_cache_max_capacity),
        datapath_(std::move(datapath)) {
    ASSERT(datapath_ != nullptr);
  }

  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;
  Api::IoCallUint64Result close() override;
  IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  IoHandlePtr duplicate() override;

  // The destructor removes the registration too, so a handle dropped without an explicit close
  // still evicts its key. The base destructor closes the fd, but removal only needs the captured
  // addresses, so order does not matter.
  ~SockmapIoSocketHandle() override;

private:
  // Inlined guard so the steady state is a single branch. The resolution work stays out of line.
  void maybeRegister(const Api::IoCallUint64Result& result) {
    if (registered_) {
      return;
    }
    registerOnFirstTransfer(result);
  }
  void registerOnFirstTransfer(const Api::IoCallUint64Result& result);
  // Removes the sockhash entry once using the captured addresses and clears them, so close and the
  // destructor together remove the key at most once.
  void removeRegistration();

  const BpfDatapathSharedPtr datapath_;
  bool registered_{false};
  // Local and peer addresses captured when the socket was registered, used to remove the same tuple
  // key on close. Null until a successful registration.
  Address::InstanceConstSharedPtr registered_local_;
  Address::InstanceConstSharedPtr registered_peer_;
};

} // namespace Network
} // namespace Envoy
