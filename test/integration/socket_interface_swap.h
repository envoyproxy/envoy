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

    Api::IoErrorPtr returnOverride(Envoy::Network::TestIoSocketHandle* io_handle) {
      absl::MutexLock lock(&mutex_);
      if (socket_type_ == io_handle->getSocketType() && error_ &&
          (io_handle->localAddress()->ip()->port() == src_port_ ||
           (dst_port_ && io_handle->peerAddress()->ip()->port() == dst_port_))) {
        ASSERT(matched_iohandle_ == nullptr || matched_iohandle_ == io_handle,
               "Matched multiple io_handles, expected at most one to match.");
        matched_iohandle_ = io_handle;
        return (error_->getSystemErrorCode() == EAGAIN)
                   ? Envoy::Network::IoSocketError::getIoSocketEagainError()
                   : Envoy::Network::IoSocketError::create(error_->getSystemErrorCode());
      }
      return Api::IoError::none();
    }

    Api::IoErrorPtr returnConnectOverride(Envoy::Network::TestIoSocketHandle* io_handle) {
      absl::MutexLock lock(&mutex_);
      if (block_connect_ && socket_type_ == io_handle->getSocketType() &&
          (io_handle->localAddress()->ip()->port() == src_port_ ||
           (dst_port_ && io_handle->peerAddress()->ip()->port() == dst_port_))) {
        return Network::IoSocketError::getIoSocketEagainError();
      }
      return Api::IoError::none();
    }

    // Source port to match. The port specified should be associated with a listener.
    void setSourcePort(uint32_t port) {
      absl::WriterMutexLock lock(&mutex_);
      dst_port_ = 0;
      src_port_ = port;
    }

    // Destination port to match. The port specified should be associated with a listener.
    void setDestinationPort(uint32_t port) {
      absl::WriterMutexLock lock(&mutex_);
      src_port_ = 0;
      dst_port_ = port;
    }

    void setWriteReturnsEgain() {
      setWriteOverride(Network::IoSocketError::getIoSocketEagainError());
    }

    // The caller is responsible for memory management.
    void setWriteOverride(Api::IoErrorPtr error) {
      absl::WriterMutexLock lock(&mutex_);
      ASSERT(src_port_ != 0 || dst_port_ != 0);
      error_ = std::move(error);
    }

    void setConnectBlock(bool block) {
      absl::WriterMutexLock lock(&mutex_);
      ASSERT(src_port_ != 0 || dst_port_ != 0);
      block_connect_ = block;
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
  };

  explicit SocketInterfaceSwap(Network::Socket::Type socket_type);

  ~SocketInterfaceSwap() {
    test_socket_interface_loader_.reset();
    Envoy::Network::SocketInterfaceSingleton::initialize(previous_socket_interface_);
  }

  Envoy::Network::SocketInterface* const previous_socket_interface_{
      Envoy::Network::SocketInterfaceSingleton::getExisting()};
  std::shared_ptr<IoHandleMatcher> write_matcher_;
  std::unique_ptr<Envoy::Network::SocketInterfaceLoader> test_socket_interface_loader_;
};

} // namespace Envoy
