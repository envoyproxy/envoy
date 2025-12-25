#include <algorithm>

#include "envoy/router/string_accessor.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
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

void envoy_dynamic_module_callback_network_filter_close_with_details(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_connection_close_type close_type,
    envoy_dynamic_module_type_buffer_module_ptr details, size_t details_length) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  if (details != nullptr && details_length > 0) {
    absl::string_view details_view(details, details_length);
    filter->connection().streamInfo().setConnectionTerminationDetails(details_view);
  }
  filter->close(toEnvoyCloseType(close_type));
}

size_t envoy_dynamic_module_callback_network_filter_get_requested_server_name(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  absl::string_view sni = filter->connection().requestedServerName();
  *result_out = const_cast<char*>(sni.data());
  return sni.size();
}

size_t envoy_dynamic_module_callback_network_filter_get_direct_remote_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_envoy_ptr* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto& address =
      filter->connection().streamInfo().downstreamAddressProvider().directRemoteAddress();

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

bool envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer** sans_out, size_t* sans_count_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto ssl = filter->connection().ssl();
  if (!ssl) {
    *sans_out = nullptr;
    *sans_count_out = 0;
    return false;
  }

  const auto& uri_sans = ssl->uriSanPeerCertificate();
  if (uri_sans.empty()) {
    *sans_out = nullptr;
    *sans_count_out = 0;
    return true;
  }

  // Allocate static thread-local storage for the result.
  static thread_local std::vector<envoy_dynamic_module_type_envoy_buffer> san_buffers;
  san_buffers.clear();
  san_buffers.reserve(uri_sans.size());

  for (const auto& san : uri_sans) {
    envoy_dynamic_module_type_envoy_buffer buf;
    buf.ptr = const_cast<char*>(san.data());
    buf.length = san.size();
    san_buffers.push_back(buf);
  }

  *sans_out = san_buffers.data();
  *sans_count_out = san_buffers.size();
  return true;
}

bool envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer** sans_out, size_t* sans_count_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto ssl = filter->connection().ssl();
  if (!ssl) {
    *sans_out = nullptr;
    *sans_count_out = 0;
    return false;
  }

  const auto& dns_sans = ssl->dnsSansPeerCertificate();
  if (dns_sans.empty()) {
    *sans_out = nullptr;
    *sans_count_out = 0;
    return true;
  }

  // Allocate static thread-local storage for the result.
  static thread_local std::vector<envoy_dynamic_module_type_envoy_buffer> san_buffers;
  san_buffers.clear();
  san_buffers.reserve(dns_sans.size());

  for (const auto& san : dns_sans) {
    envoy_dynamic_module_type_envoy_buffer buf;
    buf.ptr = const_cast<char*>(san.data());
    buf.length = san.size();
    san_buffers.push_back(buf);
  }

  *sans_out = san_buffers.data();
  *sans_count_out = san_buffers.size();
  return true;
}

size_t envoy_dynamic_module_callback_network_filter_get_ssl_subject(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto ssl = filter->connection().ssl();
  if (!ssl) {
    *result_out = nullptr;
    return 0;
  }

  const std::string& subject = ssl->subjectPeerCertificate();
  *result_out = const_cast<char*>(subject.c_str());
  return subject.size();
}

bool envoy_dynamic_module_callback_network_set_filter_state_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value_ptr, size_t value_length) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto& stream_info = filter->connection().streamInfo();

  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);
  absl::string_view value_view(static_cast<const char*>(value_ptr), value_length);

  stream_info.filterState()->setData(
      key_view, std::make_unique<Router::StringAccessorImpl>(value_view),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);
  return true;
}

bool envoy_dynamic_module_callback_network_get_filter_state_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result, size_t* result_length) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto& stream_info = filter->connection().streamInfo();

  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);
  auto filter_state = stream_info.filterState()->getDataReadOnly<Router::StringAccessor>(key_view);
  if (!filter_state) {
    return false;
  }

  absl::string_view str = filter_state->asString();
  *result = const_cast<char*>(str.data());
  *result_length = str.size();
  return true;
}

bool envoy_dynamic_module_callback_network_set_dynamic_metadata_string(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value_ptr, size_t value_length) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto& stream_info = filter->connection().streamInfo();

  std::string namespace_str(static_cast<const char*>(namespace_ptr), namespace_length);
  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);
  absl::string_view value_view(static_cast<const char*>(value_ptr), value_length);

  // Get or create the metadata for this namespace.
  Protobuf::Struct metadata(
      (*stream_info.dynamicMetadata().mutable_filter_metadata())[namespace_str]);
  auto& fields = *metadata.mutable_fields();
  fields[std::string(key_view)].set_string_value(std::string(value_view));
  stream_info.setDynamicMetadata(namespace_str, metadata);
  return true;
}

bool envoy_dynamic_module_callback_network_get_dynamic_metadata_string(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr,
    size_t* result_buffer_length_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto& stream_info = filter->connection().streamInfo();

  std::string namespace_str(static_cast<const char*>(namespace_ptr), namespace_length);
  std::string key_str(static_cast<const char*>(key_ptr), key_length);

  const auto& metadata_map = stream_info.dynamicMetadata().filter_metadata();
  auto namespace_it = metadata_map.find(namespace_str);
  if (namespace_it == metadata_map.end()) {
    return false;
  }

  const auto& fields = namespace_it->second.fields();
  auto field_it = fields.find(key_str);
  if (field_it == fields.end()) {
    return false;
  }

  if (!field_it->second.has_string_value()) {
    return false;
  }

  const auto& value = field_it->second.string_value();
  *result_buffer_ptr = const_cast<char*>(value.data());
  *result_buffer_length_ptr = value.size();
  return true;
}

bool envoy_dynamic_module_callback_network_set_dynamic_metadata_number(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length, double value) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto& stream_info = filter->connection().streamInfo();

  std::string namespace_str(static_cast<const char*>(namespace_ptr), namespace_length);
  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);

  // Get or create the metadata for this namespace.
  Protobuf::Struct metadata(
      (*stream_info.dynamicMetadata().mutable_filter_metadata())[namespace_str]);
  auto& fields = *metadata.mutable_fields();
  fields[std::string(key_view)].set_number_value(value);
  stream_info.setDynamicMetadata(namespace_str, metadata);
  return true;
}

bool envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length, double* result) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto& stream_info = filter->connection().streamInfo();

  std::string namespace_str(static_cast<const char*>(namespace_ptr), namespace_length);
  std::string key_str(static_cast<const char*>(key_ptr), key_length);

  const auto& metadata_map = stream_info.dynamicMetadata().filter_metadata();
  auto namespace_it = metadata_map.find(namespace_str);
  if (namespace_it == metadata_map.end()) {
    return false;
  }

  const auto& fields = namespace_it->second.fields();
  auto field_it = fields.find(key_str);
  if (field_it == fields.end()) {
    return false;
  }

  if (!field_it->second.has_number_value()) {
    return false;
  }

  *result = field_it->second.number_value();
  return true;
}

} // extern "C"

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
