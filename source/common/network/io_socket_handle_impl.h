#pragma once

#include <memory>

#include "envoy/api/io_error.h"
#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/io_handle.h"

#include "source/common/common/logger.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/io_socket_handle_base_impl.h"
#include "source/common/runtime/runtime_features.h"

#include "quiche/quic/core/quic_lru_cache.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace Envoy {
namespace Network {

using AddressInstanceLRUCache =
    quic::QuicLRUCache<quic::QuicSocketAddress, Address::InstanceConstSharedPtr,
                       quic::QuicSocketAddressHash>;

/**
 * IoHandle derivative for sockets.
 */
class IoSocketHandleImpl : public IoSocketHandleBaseImpl {
public:
  explicit IoSocketHandleImpl(os_fd_t fd = INVALID_SOCKET, bool socket_v6only = false,
                              absl::optional<int> domain = absl::nullopt,
                              size_t address_cache_max_capacity = 0)
      : IoSocketHandleBaseImpl(fd, socket_v6only, domain),
        receive_ecn_(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.quic_receive_ecn")) {
    if (address_cache_max_capacity > 0) {
      recent_received_addresses_ =
          std::make_unique<AddressInstanceLRUCache>(address_cache_max_capacity);
    }
  }

  // Close underlying socket if close() hasn't been call yet.
  ~IoSocketHandleImpl() override;

  Api::IoCallUint64Result close() override;

  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               absl::optional<uint64_t> max_length) override;

  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;

  Api::IoCallUint64Result write(Buffer::Instance& buffer) override;

  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
                                  const Address::Ip* self_ip,
                                  const Address::Instance& peer_address) override;

  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                  uint32_t self_port, const UdpSaveCmsgConfig& save_cmsg_config,
                                  RecvMsgOutput& output) override;

  Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                   const UdpSaveCmsgConfig& save_cmsg_config,
                                   RecvMsgOutput& output) override;
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override;

  Api::SysCallIntResult bind(Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult listen(int backlog) override;
  IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  Api::SysCallIntResult connect(Address::InstanceConstSharedPtr address) override;
  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;

  IoHandlePtr duplicate() override;

  void activateFileEvents(uint32_t events) override;
  void enableFileEvents(uint32_t events) override;

  void resetFileEvents() override { file_event_.reset(); }

  Api::SysCallIntResult shutdown(int how) override;

protected:
  // Converts a SysCallSizeResult to IoCallUint64Result.
  template <typename T>
  Api::IoCallUint64Result sysCallResultToIoCallResult(const Api::SysCallResult<T>& result) {
    if (result.return_value_ >= 0) {
      // Return nullptr as IoError upon success.
      return Api::IoCallUint64Result(result.return_value_, Api::IoError::none());
    }
    if (result.errno_ == SOCKET_ERROR_INVAL) {
      ENVOY_LOG(error, "Invalid argument passed in.");
    }
    return Api::IoCallUint64Result(
        /*rc=*/0, (result.errno_ == SOCKET_ERROR_AGAIN
                       // EAGAIN is frequent enough that its memory allocation should be avoided.
                       ? IoSocketError::getIoSocketEagainError()
                       : IoSocketError::create(result.errno_)));
  }

  Event::FileEventPtr file_event_{nullptr};

  // The minimum cmsg buffer size to filled in destination address, packets dropped and gso
  // size when receiving a packet. It is possible for a received packet to contain both IPv4
  // and IPV6 addresses.
  const size_t cmsg_space_{CMSG_SPACE(sizeof(int)) + CMSG_SPACE(sizeof(struct in_pktinfo)) +
                           CMSG_SPACE(sizeof(struct in6_pktinfo)) + CMSG_SPACE(sizeof(uint16_t))};

  // Latches a copy of the runtime feature "envoy.reloadable_features.quic_receive_ecn".
  const bool receive_ecn_;

  size_t addressCacheMaxSize() const {
    return recent_received_addresses_ == nullptr ? 0 : recent_received_addresses_->MaxSize();
  }

private:
  // Returns the destination address if the control message carries it.
  // Otherwise returns nullptr.
  Address::InstanceConstSharedPtr maybeGetDstAddressFromHeader(const cmsghdr& cmsg,
                                                               uint32_t self_port);

  Address::InstanceConstSharedPtr getOrCreateEnvoyAddressInstance(sockaddr_storage ss,
                                                                  socklen_t ss_len);

  // Caches the address instances of the most recently received packets on this socket.
  // Should only be used by UDP sockets to avoid creating multiple address instances for the same
  // address in each read operation. Only be instantiated if the non-zero address_cache_max_capacity
  // is passed in during the construction.
  std::unique_ptr<AddressInstanceLRUCache> recent_received_addresses_;
};
} // namespace Network
} // namespace Envoy
