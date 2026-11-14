#include "source/extensions/network/socket_interface/dynamic_modules/dynamic_module_io_handle.h"

#include "source/common/common/assert.h"
#include "source/common/network/io_socket_error_impl.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

namespace {

// Maps a module I/O hook outcome to the IoCallUint64Result Envoy consumes, following the
// transport socket toIoStatus precedent. Error maps to a connection-reset-like error so Envoy
// closes the connection.
Api::IoCallUint64Result toIoCallResult(envoy_dynamic_module_type_socket_interface_io_status status,
                                       uint64_t bytes_transferred) {
  switch (status) {
  case envoy_dynamic_module_type_socket_interface_io_status_Success:
    return Api::IoCallUint64Result(bytes_transferred, Api::IoError::none());
  case envoy_dynamic_module_type_socket_interface_io_status_Again:
    // EAGAIN is frequent enough that its memory allocation should be avoided.
    return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEagainError());
  case envoy_dynamic_module_type_socket_interface_io_status_Error:
    return Api::IoCallUint64Result(0, Network::IoSocketError::create(SOCKET_ERROR_CONNRESET));
  }
  IS_ENVOY_BUG("Unexpected socket interface I/O status");
  return Api::IoCallUint64Result(0, Network::IoSocketError::create(SOCKET_ERROR_CONNRESET));
}

// Maps the module readiness mask to the Envoy file-ready mask.
uint32_t toFileReadyEvents(uint32_t events) {
  uint32_t file_events = 0;
  if (events & envoy_dynamic_module_type_socket_interface_io_event_Read) {
    file_events |= Event::FileReadyType::Read;
  }
  if (events & envoy_dynamic_module_type_socket_interface_io_event_Write) {
    file_events |= Event::FileReadyType::Write;
  }
  return file_events;
}

} // namespace

void SocketSharedState::onReady(uint32_t events) {
  if (closed_.load(std::memory_order_acquire)) {
    return;
  }
  auto* handle = handle_.load(std::memory_order_acquire);
  if (handle == nullptr) {
    return;
  }
  const uint32_t file_events = toFileReadyEvents(events);
  if (file_events != 0) {
    handle->activateFileEvents(file_events);
  }
}

void SocketReadyHandle::commit(uint32_t events) {
  auto state = state_.lock();
  if (!state) {
    return;
  }
  Event::Dispatcher* dispatcher = state->dispatcher();
  if (dispatcher == nullptr) {
    return;
  }
  dispatcher->post([state, events]() { state->onReady(events); });
}

DynamicModuleIoHandle::DynamicModuleIoHandle(
    DynamicModuleSocketInterfaceConfigSharedPtr config, os_fd_t fd, bool socket_v6only,
    std::optional<int> domain, const Network::SocketCreationOptions& options,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr,
    bool already_connected)
    : Network::IoSocketHandleImpl(fd, socket_v6only, domain, options.max_addresses_cache_size_),
      config_(std::move(config)), shared_state_(std::make_shared<SocketSharedState>()),
      socket_module_ptr_(socket_module_ptr), already_connected_(already_connected) {
  shared_state_->setHandle(this);
}

DynamicModuleIoHandle::~DynamicModuleIoHandle() {
  shared_state_->clearHandle();
  releaseSocketModulePtr();
}

void DynamicModuleIoHandle::releaseSocketModulePtr() {
  if (socket_module_ptr_ != nullptr) {
    config_->on_socket_close_(config_->in_module_config_, socket_module_ptr_);
    socket_module_ptr_ = nullptr;
  }
}

Api::IoCallUint64Result DynamicModuleIoHandle::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                                     uint64_t num_slice) {
  if (max_length == 0 || num_slice == 0) {
    return Api::ioCallUint64ResultNoError();
  }
  absl::FixedArray<envoy_dynamic_module_type_socket_interface_io_slice> io_slices(num_slice);
  uint64_t num_slices_to_read = 0;
  uint64_t total_length = 0;
  for (; num_slices_to_read < num_slice && total_length < max_length; num_slices_to_read++) {
    io_slices[num_slices_to_read].ptr = static_cast<char*>(slices[num_slices_to_read].mem_);
    const size_t length =
        std::min(slices[num_slices_to_read].len_, static_cast<size_t>(max_length - total_length));
    io_slices[num_slices_to_read].length = length;
    total_length += length;
  }

  uint64_t bytes_transferred = 0;
  const auto status =
      config_->on_io_read_(config_->in_module_config_, socket_module_ptr_, io_slices.data(),
                           num_slices_to_read, &bytes_transferred);
  ENVOY_BUG(bytes_transferred <= total_length,
            fmt::format("Module read {} bytes but only offered capacity for {}", bytes_transferred,
                        total_length));
  return toIoCallResult(status, std::min(bytes_transferred, total_length));
}

Api::IoCallUint64Result DynamicModuleIoHandle::writev(const Buffer::RawSlice* slices,
                                                      uint64_t num_slice) {
  absl::FixedArray<envoy_dynamic_module_type_envoy_buffer> buffers(num_slice);
  uint64_t num_buffers = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      buffers[num_buffers].ptr = static_cast<const char*>(slices[i].mem_);
      buffers[num_buffers].length = slices[i].len_;
      num_buffers++;
    }
  }
  if (num_buffers == 0) {
    return Api::ioCallUint64ResultNoError();
  }

  uint64_t bytes_transferred = 0;
  const auto status = config_->on_io_write_(config_->in_module_config_, socket_module_ptr_,
                                            buffers.data(), num_buffers, &bytes_transferred);
  return toIoCallResult(status, bytes_transferred);
}

Api::IoCallUint64Result DynamicModuleIoHandle::close() {
  if (!SOCKET_VALID(fd_)) {
    return Api::ioCallUint64ResultNoError();
  }
  shared_state_->markClosed();
  shared_state_->clearDispatcher();
  releaseSocketModulePtr();
  return Network::IoSocketHandleImpl::close();
}

Api::SysCallIntResult DynamicModuleIoHandle::shutdown(int how) {
  if (config_->on_io_shutdown_ == nullptr) {
    return Network::IoSocketHandleImpl::shutdown(how);
  }
  const auto status = config_->on_io_shutdown_(config_->in_module_config_, socket_module_ptr_, how);
  if (status == envoy_dynamic_module_type_socket_interface_io_status_Success) {
    return {0, 0};
  }
  return {-1, SOCKET_ERROR_CONNRESET};
}

Api::SysCallIntResult
DynamicModuleIoHandle::connect(Network::Address::InstanceConstSharedPtr address) {
  if (already_connected_) {
    return {0, 0};
  }
  return Network::IoSocketHandleImpl::connect(address);
}

void DynamicModuleIoHandle::initializeFileEvent(Event::Dispatcher& dispatcher,
                                                Event::FileReadyCb cb,
                                                Event::FileTriggerType trigger, uint32_t events) {
  Network::IoSocketHandleImpl::initializeFileEvent(dispatcher, std::move(cb), trigger, events);
  shared_state_->setDispatcher(dispatcher);
}

void DynamicModuleIoHandle::resetFileEvents() {
  shared_state_->clearDispatcher();
  Network::IoSocketHandleImpl::resetFileEvents();
}

} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
