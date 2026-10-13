#pragma once

#include "source/common/network/socket_interface.h"

#include "test/integration/filters/test_socket_interface.h"

namespace Envoy {

// Enables control at the socket level to stop and resume writes.
// Useful for tests want to temporarily stop Envoy from draining data.

class SocketInterfaceSwap {
public:
  // Object of this class hold the state determining the IoHandle which
  // should return the supplied return from the `writev` or `sendmsg` calls.
  struct IoHandleMatcher {
    explicit IoHandleMatcher(Network::Socket::Type type) : socket_type_(type) {}

    Api::IoErrorPtr
    returnOverride(Envoy::Network::TestIoSocketHandle* io_handle,
                   Network::Address::InstanceConstSharedPtr& peer_address_override_out) {
      absl::MutexLock lock(mutex_);
      if (absl::StatusOr<Network::Address::InstanceConstSharedPtr> addr = io_handle->localAddress();
          socket_type_ == io_handle->getSocketType() && error_ &&
          ((addr.ok() && (*addr)->ip()->port() == src_port_) ||
           (dst_port_ && (*io_handle->peerAddress())->ip()->port() == dst_port_))) {
        ASSERT(matched_iohandle_ == nullptr || matched_iohandle_ == io_handle,
               "Matched multiple io_handles, expected at most one to match.");
        matched_iohandle_ = io_handle;
        return (error_->getSystemErrorCode() == EAGAIN)
                   ? Envoy::Network::IoSocketError::getIoSocketEagainError()
                   : Envoy::Network::IoSocketError::create(error_->getSystemErrorCode());
      }

      if (orig_dnat_address_ != nullptr && *orig_dnat_address_ == **io_handle->peerAddress()) {
        ASSERT(translated_dnat_address_ != nullptr);
        peer_address_override_out = translated_dnat_address_;
      }

      return Api::IoError::none();
    }

    Api::IoErrorPtr
    returnConnectOverride(Envoy::Network::TestIoSocketHandle* io_handle,
                          Network::Address::InstanceConstSharedPtr& peer_address_override_out) {
      absl::MutexLock lock(mutex_);
      if (absl::StatusOr<Network::Address::InstanceConstSharedPtr> addr = io_handle->localAddress();
          block_connect_ && socket_type_ == io_handle->getSocketType() &&
          ((addr.ok() && (*addr)->ip()->port() == src_port_) ||
           (dst_port_ && (*io_handle->peerAddress())->ip()->port() == dst_port_))) {
        return Network::IoSocketError::getIoSocketEagainError();
      }

      if (orig_dnat_address_ != nullptr && peer_address_override_out != nullptr &&
          *orig_dnat_address_ == *peer_address_override_out) {
        ASSERT(translated_dnat_address_ != nullptr);
        peer_address_override_out = translated_dnat_address_;
      }

      return Api::IoError::none();
    }

    void readOverride(Network::IoHandle::RecvMsgOutput& output) {
      absl::MutexLock lock(mutex_);
      if (translated_dnat_address_ != nullptr) {
        for (auto& pkt : output.msg_) {
          // Reverse DNAT when receiving packets.
          if (pkt.peer_address_ != nullptr && *pkt.peer_address_ == *translated_dnat_address_) {
            ASSERT(orig_dnat_address_ != nullptr);
            pkt.peer_address_ = orig_dnat_address_;
          }
        }
      }
    }

    // Source port to match. The port specified should be associated with a listener.
    void setSourcePort(uint32_t port) {
      absl::WriterMutexLock lock(mutex_);
      dst_port_ = 0;
      src_port_ = port;
    }

    // Destination port to match. The port specified should be associated with a listener.
    void setDestinationPort(uint32_t port) {
      absl::WriterMutexLock lock(mutex_);
      src_port_ = 0;
      dst_port_ = port;
    }

    void setWriteReturnsEgain() {
      setWriteOverride(Network::IoSocketError::getIoSocketEagainError());
    }

    // The caller is responsible for memory management.
    void setWriteOverride(Api::IoErrorPtr error) {
      absl::WriterMutexLock lock(mutex_);
      ASSERT(src_port_ != 0 || dst_port_ != 0);
      error_ = std::move(error);
    }

    void setConnectBlock(bool block) {
      absl::WriterMutexLock lock(mutex_);
      ASSERT(src_port_ != 0 || dst_port_ != 0);
      block_connect_ = block;
    }

    void setDnat(Network::Address::InstanceConstSharedPtr orig_address,
                 Network::Address::InstanceConstSharedPtr translated_address) {
      absl::WriterMutexLock lock(mutex_);
      orig_dnat_address_ = orig_address;
      translated_dnat_address_ = translated_address;
    }

    void setResumeWrites();

  private:
    mutable absl::Mutex mutex_;
    uint32_t src_port_ ABSL_GUARDED_BY(mutex_) = 0;
    uint32_t dst_port_ ABSL_GUARDED_BY(mutex_) = 0;
    Api::IoErrorPtr error_ ABSL_GUARDED_BY(mutex_) = Api::IoError::none();
    Network::TestIoSocketHandle* matched_iohandle_{};
    Network::Socket::Type socket_type_;
    bool block_connect_ ABSL_GUARDED_BY(mutex_) = false;
    Network::Address::InstanceConstSharedPtr orig_dnat_address_ ABSL_GUARDED_BY(mutex_);
    Network::Address::InstanceConstSharedPtr translated_dnat_address_ ABSL_GUARDED_BY(mutex_);
  };

  explicit SocketInterfaceSwap(Network::Socket::Type socket_type);

  std::shared_ptr<IoHandleMatcher> write_matcher_;
  StackedScopedInjectableLoaderForTest<Network::SocketInterface> test_socket_interface_loader_;
};

} // namespace Envoy
