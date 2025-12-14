#include <algorithm>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/network/dynamic_modules/filter.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace NetworkFilters {

namespace {

Network::ConnectionCloseType
toEnvoyCloseType(envoy_dynamic_module_type_network_connection_close_type close_type) {
  switch (close_type) {
  case envoy_dynamic_module_type_network_connection_close_type_FlushWrite:
    return Network::ConnectionCloseType::FlushWrite;
  case envoy_dynamic_module_type_network_connection_close_type_NoFlush:
    return Network::ConnectionCloseType::NoFlush;
  case envoy_dynamic_module_type_network_connection_close_type_FlushWriteAndDelay:
    return Network::ConnectionCloseType::FlushWriteAndDelay;
  case envoy_dynamic_module_type_network_connection_close_type_Abort:
    return Network::ConnectionCloseType::Abort;
  case envoy_dynamic_module_type_network_connection_close_type_AbortReset:
    return Network::ConnectionCloseType::AbortReset;
  }
  return Network::ConnectionCloseType::NoFlush;
}

// Helper to get buffer slices from a Buffer::Instance.
void getBufferSlices(Buffer::Instance& buffer,
                     std::vector<envoy_dynamic_module_type_envoy_buffer>& slices) {
  const uint64_t num_slices = buffer.getRawSlices().size();
  if (num_slices == 0) {
    return;
  }

  Buffer::RawSliceVector raw_slices = buffer.getRawSlices();
  slices.reserve(raw_slices.size());
  for (const auto& slice : raw_slices) {
    envoy_dynamic_module_type_envoy_buffer envoy_buffer;
    envoy_buffer.ptr = static_cast<char*>(slice.mem_);
    envoy_buffer.length = slice.len_;
    slices.push_back(envoy_buffer);
  }
}

} // namespace

extern "C" {

size_t envoy_dynamic_module_callback_network_filter_get_read_buffer_slices(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer** result_buffer_ptr, size_t* result_buffer_length_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();

  if (buffer == nullptr) {
    *result_buffer_ptr = nullptr;
    *result_buffer_length_ptr = 0;
    return 0;
  }

  // Get the slices. Please note that here we allocate memory here that the
  // module must not free. These slices are valid until the end of the callback.
  static thread_local std::vector<envoy_dynamic_module_type_envoy_buffer> slices;
  slices.clear();
  getBufferSlices(*buffer, slices);

  if (slices.empty()) {
    *result_buffer_ptr = nullptr;
    *result_buffer_length_ptr = 0;
    return 0;
  }

  *result_buffer_ptr = slices.data();
  *result_buffer_length_ptr = slices.size();
  return buffer->length();
}

size_t envoy_dynamic_module_callback_network_filter_get_write_buffer_slices(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer** result_buffer_ptr, size_t* result_buffer_length_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();

  if (buffer == nullptr) {
    *result_buffer_ptr = nullptr;
    *result_buffer_length_ptr = 0;
    return 0;
  }

  // Get the slices. Please note that here we allocate memory here that the
  // module must not free. These slices are valid until the end of the callback.
  static thread_local std::vector<envoy_dynamic_module_type_envoy_buffer> slices;
  slices.clear();
  getBufferSlices(*buffer, slices);

  if (slices.empty()) {
    *result_buffer_ptr = nullptr;
    *result_buffer_length_ptr = 0;
    return 0;
  }

  *result_buffer_ptr = slices.data();
  *result_buffer_length_ptr = slices.size();
  return buffer->length();
}

void envoy_dynamic_module_callback_network_filter_drain_read_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t length) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();
  if (buffer != nullptr && length > 0) {
    buffer->drain(std::min(static_cast<uint64_t>(length), buffer->length()));
  }
}

void envoy_dynamic_module_callback_network_filter_drain_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t length) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();
  if (buffer != nullptr && length > 0) {
    buffer->drain(std::min(static_cast<uint64_t>(length), buffer->length()));
  }
}

void envoy_dynamic_module_callback_network_filter_prepend_read_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();
  if (buffer != nullptr && data != nullptr && length > 0) {
    buffer->prepend(absl::string_view(data, length));
  }
}

void envoy_dynamic_module_callback_network_filter_append_read_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();
  if (buffer != nullptr && data != nullptr && length > 0) {
    buffer->add(data, length);
  }
}

void envoy_dynamic_module_callback_network_filter_prepend_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();
  if (buffer != nullptr && data != nullptr && length > 0) {
    buffer->prepend(absl::string_view(data, length));
  }
}

void envoy_dynamic_module_callback_network_filter_append_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();
  if (buffer != nullptr && data != nullptr && length > 0) {
    buffer->add(data, length);
  }
}

void envoy_dynamic_module_callback_network_filter_write(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length, bool end_stream) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  if (data != nullptr && length > 0) {
    Buffer::OwnedImpl buffer;
    buffer.add(data, length);
    filter->write(buffer, end_stream);
  } else if (end_stream) {
    Buffer::OwnedImpl empty;
    filter->write(empty, true);
  }
}

void envoy_dynamic_module_callback_network_filter_inject_read_data(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length, bool end_stream) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->readCallbacks();
  if (callbacks != nullptr) {
    Buffer::OwnedImpl buffer;
    if (data != nullptr && length > 0) {
      buffer.add(data, length);
    }
    callbacks->injectReadDataToFilterChain(buffer, end_stream);
  }
}

void envoy_dynamic_module_callback_network_filter_inject_write_data(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length, bool end_stream) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->writeCallbacks();
  if (callbacks != nullptr) {
    Buffer::OwnedImpl buffer;
    if (data != nullptr && length > 0) {
      buffer.add(data, length);
    }
    callbacks->injectWriteDataToFilterChain(buffer, end_stream);
  }
}

void envoy_dynamic_module_callback_network_filter_continue_reading(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  filter->continueReading();
}

void envoy_dynamic_module_callback_network_filter_close(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_connection_close_type close_type) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  filter->close(toEnvoyCloseType(close_type));
}

uint64_t envoy_dynamic_module_callback_network_filter_get_connection_id(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  return filter->connection().id();
}

size_t envoy_dynamic_module_callback_network_filter_get_remote_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_envoy_ptr* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto& address = filter->connection().connectionInfoProvider().remoteAddress();

  if (address == nullptr || address->ip() == nullptr) {
    *address_out = nullptr;
    *port_out = 0;
    return 0;
  }

  const std::string& addr_str = address->ip()->addressAsString();
  *address_out = const_cast<char*>(addr_str.c_str());
  *port_out = address->ip()->port();
  return addr_str.size();
}

size_t envoy_dynamic_module_callback_network_filter_get_local_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_envoy_ptr* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto& address = filter->connection().connectionInfoProvider().localAddress();

  if (address == nullptr || address->ip() == nullptr) {
    *address_out = nullptr;
    *port_out = 0;
    return 0;
  }

  const std::string& addr_str = address->ip()->addressAsString();
  *address_out = const_cast<char*>(addr_str.c_str());
  *port_out = address->ip()->port();
  return addr_str.size();
}

bool envoy_dynamic_module_callback_network_filter_is_ssl(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  return filter->connection().ssl() != nullptr;
}

void envoy_dynamic_module_callback_network_filter_disable_close(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, bool disabled) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->readCallbacks();
  if (callbacks != nullptr) {
    callbacks->disableClose(disabled);
  }
}

} // extern "C"

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
