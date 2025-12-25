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

// Helper to fill buffer chunks from a Buffer::Instance into a pre-allocated array.
void fillBufferChunks(const Buffer::Instance& buffer,
                      envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  Buffer::RawSliceVector raw_slices = buffer.getRawSlices();
  auto counter = 0;
  for (const auto& slice : raw_slices) {
    result_buffer_vector[counter].length = slice.len_;
    result_buffer_vector[counter].ptr = static_cast<char*>(slice.mem_);
    counter++;
  }
}

} // namespace

extern "C" {

bool envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();

  if (buffer == nullptr) {
    return false;
  }
  *size = buffer->getRawSlices(std::nullopt).size();
  return true;
}

size_t envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();

  if (buffer == nullptr) {
    return 0;
  }

  fillBufferChunks(*buffer, result_buffer_vector);
  return buffer->length();
}

bool envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();

  if (buffer == nullptr) {
    return false;
  }
  *size = buffer->getRawSlices(std::nullopt).size();
  return true;
}

size_t envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();

  if (buffer == nullptr) {
    return 0;
  }

  fillBufferChunks(*buffer, result_buffer_vector);
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
    envoy_dynamic_module_type_module_buffer data) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();
  if (buffer != nullptr && data.ptr != nullptr && data.length > 0) {
    buffer->prepend(absl::string_view(data.ptr, data.length));
  }
}

void envoy_dynamic_module_callback_network_filter_append_read_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();
  if (buffer != nullptr && data.ptr != nullptr && data.length > 0) {
    buffer->add(data.ptr, data.length);
  }
}

void envoy_dynamic_module_callback_network_filter_prepend_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();
  if (buffer != nullptr && data.ptr != nullptr && data.length > 0) {
    buffer->prepend(absl::string_view(data.ptr, data.length));
  }
}

void envoy_dynamic_module_callback_network_filter_append_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();
  if (buffer != nullptr && data.ptr != nullptr && data.length > 0) {
    buffer->add(data.ptr, data.length);
  }
}

void envoy_dynamic_module_callback_network_filter_write(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  if (data.ptr != nullptr && data.length > 0) {
    Buffer::OwnedImpl buffer;
    buffer.add(data.ptr, data.length);
    filter->write(buffer, end_stream);
  } else if (end_stream) {
    Buffer::OwnedImpl empty;
    filter->write(empty, true);
  }
}

void envoy_dynamic_module_callback_network_filter_inject_read_data(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->readCallbacks();
  if (callbacks != nullptr) {
    Buffer::OwnedImpl buffer;
    if (data.ptr != nullptr && data.length > 0) {
      buffer.add(data.ptr, data.length);
    }
    callbacks->injectReadDataToFilterChain(buffer, end_stream);
  }
}

void envoy_dynamic_module_callback_network_filter_inject_write_data(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->writeCallbacks();
  if (callbacks != nullptr) {
    Buffer::OwnedImpl buffer;
    if (data.ptr != nullptr && data.length > 0) {
      buffer.add(data.ptr, data.length);
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
