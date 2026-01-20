#include <algorithm>
#include <cstddef>

#include "envoy/config/core/v3/socket_option.pb.h"
#include "envoy/http/message.h"
#include "envoy/router/string_accessor.h"

#include "source/common/http/message_impl.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/network/upstream_socket_options_filter_state.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/network/dynamic_modules/filter.h"
#include "source/extensions/filters/network/dynamic_modules/filter_config.h"

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

size_t envoy_dynamic_module_callback_network_filter_get_read_buffer_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();

  if (buffer == nullptr) {
    return 0;
  }
  return buffer->length();
}

size_t envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();
  if (buffer == nullptr) {
    return 0;
  }
  return buffer->getRawSlices(std::nullopt).size();
}

bool envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();
  if (buffer == nullptr) {
    return false;
  }

  fillBufferChunks(*buffer, result_buffer_vector);
  return true;
}

size_t envoy_dynamic_module_callback_network_filter_get_write_buffer_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();
  if (buffer == nullptr) {
    return 0;
  }
  return buffer->length();
}

size_t envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();

  if (buffer == nullptr) {
    return 0;
  }
  return buffer->getRawSlices(std::nullopt).size();
}

bool envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();

  if (buffer == nullptr) {
    return false;
  }

  fillBufferChunks(*buffer, result_buffer_vector);
  return true;
}

bool envoy_dynamic_module_callback_network_filter_drain_read_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t length) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();
  if (buffer != nullptr && length > 0) {
    buffer->drain(std::min(static_cast<uint64_t>(length), buffer->length()));
    return true;
  }
  return false;
}

bool envoy_dynamic_module_callback_network_filter_drain_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t length) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();
  if (buffer != nullptr && length > 0) {
    buffer->drain(std::min(static_cast<uint64_t>(length), buffer->length()));
    return true;
  }
  return false;
}

bool envoy_dynamic_module_callback_network_filter_prepend_read_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();
  if (buffer != nullptr && data.ptr != nullptr && data.length > 0) {
    buffer->prepend(absl::string_view(data.ptr, data.length));
    return true;
  }
  return false;
}

bool envoy_dynamic_module_callback_network_filter_append_read_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentReadBuffer();
  if (buffer != nullptr && data.ptr != nullptr && data.length > 0) {
    buffer->add(data.ptr, data.length);
    return true;
  }
  return false;
}

bool envoy_dynamic_module_callback_network_filter_prepend_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();
  if (buffer != nullptr && data.ptr != nullptr && data.length > 0) {
    buffer->prepend(absl::string_view(data.ptr, data.length));
    return true;
  }
  return false;
}

bool envoy_dynamic_module_callback_network_filter_append_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::Instance* buffer = filter->currentWriteBuffer();
  if (buffer != nullptr && data.ptr != nullptr && data.length > 0) {
    buffer->add(data.ptr, data.length);
    return true;
  }
  return false;
}

void envoy_dynamic_module_callback_network_filter_write(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Buffer::OwnedImpl buffer;
  if (data.ptr != nullptr && data.length > 0) {
    buffer.add(data.ptr, data.length);
  }
  if (buffer.length() > 0 || end_stream) {
    filter->connection().write(buffer, end_stream);
  }
}

void envoy_dynamic_module_callback_network_filter_inject_read_data(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  if (filter->readCallbacks() == nullptr) {
    return;
  }
  Buffer::OwnedImpl buffer;
  if (data.ptr != nullptr && data.length > 0) {
    buffer.add(data.ptr, data.length);
  }
  filter->readCallbacks()->injectReadDataToFilterChain(buffer, end_stream);
}

void envoy_dynamic_module_callback_network_filter_inject_write_data(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  if (filter->writeCallbacks() == nullptr) {
    return;
  }
  Buffer::OwnedImpl buffer;
  if (data.ptr != nullptr && data.length > 0) {
    buffer.add(data.ptr, data.length);
  }
  filter->writeCallbacks()->injectWriteDataToFilterChain(buffer, end_stream);
}

void envoy_dynamic_module_callback_network_filter_continue_reading(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  if (filter->readCallbacks() != nullptr) {
    filter->readCallbacks()->continueReading();
  }
}

void envoy_dynamic_module_callback_network_filter_close(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_connection_close_type close_type) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  filter->connection().close(toEnvoyCloseType(close_type));
}

uint64_t envoy_dynamic_module_callback_network_filter_get_connection_id(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  return filter->connection().id();
}

bool envoy_dynamic_module_callback_network_filter_get_remote_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto& address = filter->connection().connectionInfoProvider().remoteAddress();

  if (address == nullptr || address->ip() == nullptr) {
    *address_out = {nullptr, 0};
    *port_out = 0;
    return false;
  }

  const std::string& addr_str = address->ip()->addressAsString();
  *address_out = {addr_str.data(), addr_str.size()};
  *port_out = address->ip()->port();
  return true;
}

bool envoy_dynamic_module_callback_network_filter_get_local_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto& address = filter->connection().connectionInfoProvider().localAddress();

  if (address == nullptr || address->ip() == nullptr) {
    *address_out = {nullptr, 0};
    *port_out = 0;
    return false;
  }

  const std::string& addr_str = address->ip()->addressAsString();
  *address_out = {addr_str.data(), addr_str.size()};
  *port_out = address->ip()->port();
  return true;
}

bool envoy_dynamic_module_callback_network_filter_is_ssl(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto ssl = filter->connection().ssl();
  return ssl != nullptr;
}

void envoy_dynamic_module_callback_network_filter_disable_close(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, bool disabled) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  if (filter->readCallbacks() != nullptr) {
    filter->readCallbacks()->disableClose(disabled);
  }
}

void envoy_dynamic_module_callback_network_filter_close_with_details(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_connection_close_type close_type,
    envoy_dynamic_module_type_module_buffer details) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  if (details.ptr != nullptr && details.length > 0) {
    filter->connection().streamInfo().setConnectionTerminationDetails(
        absl::string_view(details.ptr, details.length));
  }
  filter->connection().close(toEnvoyCloseType(close_type));
}

bool envoy_dynamic_module_callback_network_filter_get_requested_server_name(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const absl::string_view sni = filter->connection().connectionInfoProvider().requestedServerName();

  if (sni.empty()) {
    result_out->ptr = nullptr;
    result_out->length = 0;
    return false;
  }

  result_out->ptr = const_cast<char*>(sni.data());
  result_out->length = sni.size();
  return true;
}

bool envoy_dynamic_module_callback_network_filter_get_direct_remote_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto& address =
      filter->connection().streamInfo().downstreamAddressProvider().directRemoteAddress();

  if (address == nullptr || address->ip() == nullptr) {
    address_out->ptr = nullptr;
    address_out->length = 0;
    *port_out = 0;
    return false;
  }

  const std::string& addr_str = address->ip()->addressAsString();
  address_out->ptr = const_cast<char*>(addr_str.c_str());
  address_out->length = addr_str.size();
  *port_out = address->ip()->port();
  return true;
}

size_t envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto ssl = filter->connection().ssl();
  if (!ssl) {
    return 0;
  }

  return ssl->uriSanPeerCertificate().size();
}

bool envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto ssl = filter->connection().ssl();
  if (!ssl) {
    return false;
  }

  const auto& uri_sans = ssl->uriSanPeerCertificate();
  // Populate the pre-allocated array. Module is responsible for allocating the correct size.
  for (size_t i = 0; i < uri_sans.size(); ++i) {
    sans_out[i].ptr = const_cast<char*>(uri_sans[i].data());
    sans_out[i].length = uri_sans[i].size();
  }
  return true;
}

size_t envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto ssl = filter->connection().ssl();
  if (!ssl) {
    return 0;
  }

  return ssl->dnsSansPeerCertificate().size();
}

bool envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto ssl = filter->connection().ssl();
  if (!ssl) {
    return false;
  }

  const auto& dns_sans = ssl->dnsSansPeerCertificate();
  // Populate the pre-allocated array. Module is responsible for allocating the correct size.
  for (size_t i = 0; i < dns_sans.size(); ++i) {
    sans_out[i].ptr = const_cast<char*>(dns_sans[i].data());
    sans_out[i].length = dns_sans[i].size();
  }
  return true;
}

bool envoy_dynamic_module_callback_network_filter_get_ssl_subject(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  const auto ssl = filter->connection().ssl();
  if (!ssl) {
    result_out->ptr = nullptr;
    result_out->length = 0;
    return false;
  }

  const std::string& subject = ssl->subjectPeerCertificate();
  result_out->ptr = const_cast<char*>(subject.data());
  result_out->length = subject.size();
  return true;
}

bool envoy_dynamic_module_callback_network_set_filter_state_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto& stream_info = filter->connection().streamInfo();

  absl::string_view key_view(key.ptr, key.length);
  absl::string_view value_view(value.ptr, value.length);

  stream_info.filterState()->setData(
      key_view, std::make_unique<Router::StringAccessorImpl>(value_view),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);
  return true;
}

bool envoy_dynamic_module_callback_network_get_filter_state_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto& stream_info = filter->connection().streamInfo();

  absl::string_view key_view(key.ptr, key.length);
  auto filter_state = stream_info.filterState()->getDataReadOnly<Router::StringAccessor>(key_view);
  if (!filter_state) {
    return false;
  }

  absl::string_view str = filter_state->asString();
  value_out->ptr = const_cast<char*>(str.data());
  value_out->length = str.size();
  return true;
}

void envoy_dynamic_module_callback_network_set_dynamic_metadata_string(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto& stream_info = filter->connection().streamInfo();

  std::string namespace_str(filter_namespace.ptr, filter_namespace.length);
  absl::string_view key_view(key.ptr, key.length);
  absl::string_view value_view(value.ptr, value.length);

  // Get or create the metadata for this namespace.
  Protobuf::Struct metadata(
      (*stream_info.dynamicMetadata().mutable_filter_metadata())[namespace_str]);
  auto& fields = *metadata.mutable_fields();
  fields[std::string(key_view)].set_string_value(std::string(value_view));
  stream_info.setDynamicMetadata(namespace_str, metadata);
}

bool envoy_dynamic_module_callback_network_get_dynamic_metadata_string(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto& stream_info = filter->connection().streamInfo();

  std::string namespace_str(filter_namespace.ptr, filter_namespace.length);
  std::string key_str(key.ptr, key.length);

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
  value_out->ptr = const_cast<char*>(value.data());
  value_out->length = value.size();
  return true;
}

void envoy_dynamic_module_callback_network_set_dynamic_metadata_number(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, double value) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto& stream_info = filter->connection().streamInfo();

  std::string namespace_str(filter_namespace.ptr, filter_namespace.length);
  absl::string_view key_view(key.ptr, key.length);

  // Get or create the metadata for this namespace.
  Protobuf::Struct metadata(
      (*stream_info.dynamicMetadata().mutable_filter_metadata())[namespace_str]);
  auto& fields = *metadata.mutable_fields();
  fields[std::string(key_view)].set_number_value(value);
  stream_info.setDynamicMetadata(namespace_str, metadata);
}

bool envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, double* result) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto& stream_info = filter->connection().streamInfo();

  std::string namespace_str(filter_namespace.ptr, filter_namespace.length);
  std::string key_str(key.ptr, key.length);

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

envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_network_filter_http_callout(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, uint64_t* callout_id_out,
    envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, uint64_t timeout_milliseconds) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);

  // Build the request message.
  Http::RequestMessagePtr message = std::make_unique<Http::RequestMessageImpl>();

  // Add headers.
  for (size_t i = 0; i < headers_size; i++) {
    const auto& header = headers[i];
    message->headers().addCopy(
        Http::LowerCaseString(std::string(header.key_ptr, header.key_length)),
        std::string(header.value_ptr, header.value_length));
  }

  // Add body if present.
  if (body.length > 0 && body.ptr != nullptr) {
    message->body().add(body.ptr, body.length);
  }

  // Validate required headers.
  if (message->headers().Method() == nullptr || message->headers().Path() == nullptr ||
      message->headers().Host() == nullptr) {
    return envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders;
  }

  // Send the callout.
  return filter->sendHttpCallout(callout_id_out, std::string(cluster_name.ptr, cluster_name.length),
                                 std::move(message), timeout_milliseconds);
}

namespace {

Network::UpstreamSocketOptionsFilterState*
ensureUpstreamSocketOptionsFilterState(DynamicModuleNetworkFilter& filter) {
  auto filter_state_shared = filter.connection().streamInfo().filterState();
  StreamInfo::FilterState& filter_state = *filter_state_shared;
  const bool has_options = filter_state.hasData<Network::UpstreamSocketOptionsFilterState>(
      Network::UpstreamSocketOptionsFilterState::key());
  if (!has_options) {
    filter_state.setData(Network::UpstreamSocketOptionsFilterState::key(),
                         std::make_unique<Network::UpstreamSocketOptionsFilterState>(),
                         StreamInfo::FilterState::StateType::Mutable,
                         StreamInfo::FilterState::LifeSpan::Connection);
  }
  return filter_state.getDataMutable<Network::UpstreamSocketOptionsFilterState>(
      Network::UpstreamSocketOptionsFilterState::key());
}

envoy::config::core::v3::SocketOption::SocketState
mapSocketState(envoy_dynamic_module_type_socket_option_state state) {
  switch (state) {
  case envoy_dynamic_module_type_socket_option_state_Prebind:
    return envoy::config::core::v3::SocketOption::STATE_PREBIND;
  case envoy_dynamic_module_type_socket_option_state_Bound:
    return envoy::config::core::v3::SocketOption::STATE_BOUND;
  case envoy_dynamic_module_type_socket_option_state_Listening:
    return envoy::config::core::v3::SocketOption::STATE_LISTENING;
  }
  return envoy::config::core::v3::SocketOption::STATE_PREBIND;
}

bool validateSocketState(envoy_dynamic_module_type_socket_option_state state) {
  return state == envoy_dynamic_module_type_socket_option_state_Prebind ||
         state == envoy_dynamic_module_type_socket_option_state_Bound ||
         state == envoy_dynamic_module_type_socket_option_state_Listening;
}

} // namespace

void envoy_dynamic_module_callback_network_set_socket_option_int(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, envoy_dynamic_module_type_socket_option_state state, int64_t value) {
  ASSERT(validateSocketState(state));

  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto* upstream_options = ensureUpstreamSocketOptionsFilterState(*filter);

  auto option = std::make_shared<Network::SocketOptionImpl>(
      mapSocketState(state),
      Network::SocketOptionName(static_cast<int>(level), static_cast<int>(name), ""),
      static_cast<int>(value));
  Network::Socket::OptionsSharedPtr option_list = std::make_shared<Network::Socket::Options>();
  option_list->push_back(option);
  upstream_options->addOption(option_list);

  filter->storeSocketOptionInt(level, name, state, value);
}

void envoy_dynamic_module_callback_network_set_socket_option_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_module_buffer value) {
  ASSERT(validateSocketState(state));

  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto* upstream_options = ensureUpstreamSocketOptionsFilterState(*filter);

  absl::string_view value_view(value.ptr, value.length);
  auto option = std::make_shared<Network::SocketOptionImpl>(
      mapSocketState(state),
      Network::SocketOptionName(static_cast<int>(level), static_cast<int>(name), ""), value_view);
  Network::Socket::OptionsSharedPtr option_list = std::make_shared<Network::Socket::Options>();
  option_list->push_back(option);
  upstream_options->addOption(option_list);

  filter->storeSocketOptionBytes(level, name, state, value_view);
}

bool envoy_dynamic_module_callback_network_get_socket_option_int(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, envoy_dynamic_module_type_socket_option_state state, int64_t* value_out) {
  if (value_out == nullptr || !validateSocketState(state)) {
    return false;
  }
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  return filter->tryGetSocketOptionInt(level, name, state, *value_out);
}

bool envoy_dynamic_module_callback_network_get_socket_option_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  if (value_out == nullptr || !validateSocketState(state)) {
    return false;
  }
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  absl::string_view value_view;
  if (!filter->tryGetSocketOptionBytes(level, name, state, value_view)) {
    return false;
  }
  value_out->ptr = value_view.data();
  value_out->length = value_view.size();
  return true;
}

size_t envoy_dynamic_module_callback_network_get_socket_options_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  return filter->socketOptionCount();
}

void envoy_dynamic_module_callback_network_get_socket_options(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_socket_option* options_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  size_t options_written = 0;
  filter->copySocketOptions(options_out, filter->socketOptionCount(), options_written);
}

// -----------------------------------------------------------------------------
// Metrics Callbacks
// -----------------------------------------------------------------------------

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_config_define_counter(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* counter_id_ptr) {
  auto* config = static_cast<DynamicModuleNetworkFilterConfig*>(config_envoy_ptr);
  Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Stats::Counter& c = Stats::Utility::counterFromStatNames(*config->stats_scope_, {main_stat_name});
  *counter_id_ptr = config->addCounter({c});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_increment_counter(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto counter = filter->getFilterConfig().getCounterById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  counter->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_config_define_gauge(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* gauge_id_ptr) {
  auto* config = static_cast<DynamicModuleNetworkFilterConfig*>(config_envoy_ptr);
  Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Stats::Gauge& g = Stats::Utility::gaugeFromStatNames(*config->stats_scope_, {main_stat_name},
                                                       Stats::Gauge::ImportMode::Accumulate);
  *gauge_id_ptr = config->addGauge({g});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_network_filter_set_gauge(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->set(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_increment_gauge(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_decrement_gauge(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->sub(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_config_define_histogram(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* histogram_id_ptr) {
  auto* config = static_cast<DynamicModuleNetworkFilterConfig*>(config_envoy_ptr);
  Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Stats::Histogram& h = Stats::Utility::histogramFromStatNames(
      *config->stats_scope_, {main_stat_name}, Stats::Histogram::Unit::Unspecified);
  *histogram_id_ptr = config->addHistogram({h});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_record_histogram_value(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  auto histogram = filter->getFilterConfig().getHistogramById(id);
  if (!histogram.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  histogram->recordValue(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

// -----------------------------------------------------------------------------
// Upstream Host Access Callbacks
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_network_filter_get_upstream_host_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  if (filter->readCallbacks() == nullptr) {
    address_out->ptr = nullptr;
    address_out->length = 0;
    *port_out = 0;
    return false;
  }

  const auto host = filter->readCallbacks()->upstreamHost();
  if (host == nullptr) {
    address_out->ptr = nullptr;
    address_out->length = 0;
    *port_out = 0;
    return false;
  }

  const auto& address = host->address();
  if (address == nullptr || address->ip() == nullptr) {
    address_out->ptr = nullptr;
    address_out->length = 0;
    *port_out = 0;
    return false;
  }

  const std::string& addr_str = address->ip()->addressAsString();
  address_out->ptr = const_cast<char*>(addr_str.data());
  address_out->length = addr_str.size();
  *port_out = address->ip()->port();
  return true;
}

bool envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* hostname_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  if (filter->readCallbacks() == nullptr) {
    hostname_out->ptr = nullptr;
    hostname_out->length = 0;
    return false;
  }

  const auto host = filter->readCallbacks()->upstreamHost();
  if (host == nullptr) {
    hostname_out->ptr = nullptr;
    hostname_out->length = 0;
    return false;
  }

  const std::string& hostname = host->hostname();
  if (hostname.empty()) {
    hostname_out->ptr = nullptr;
    hostname_out->length = 0;
    return false;
  }

  hostname_out->ptr = const_cast<char*>(hostname.data());
  hostname_out->length = hostname.size();
  return true;
}

bool envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* cluster_name_out) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  if (filter->readCallbacks() == nullptr) {
    cluster_name_out->ptr = nullptr;
    cluster_name_out->length = 0;
    return false;
  }

  const auto host = filter->readCallbacks()->upstreamHost();
  if (host == nullptr) {
    cluster_name_out->ptr = nullptr;
    cluster_name_out->length = 0;
    return false;
  }

  const std::string& cluster_name = host->cluster().name();
  cluster_name_out->ptr = const_cast<char*>(cluster_name.data());
  cluster_name_out->length = cluster_name.size();
  return true;
}

bool envoy_dynamic_module_callback_network_filter_has_upstream_host(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  if (filter->readCallbacks() == nullptr) {
    return false;
  }

  return filter->readCallbacks()->upstreamHost() != nullptr;
}

// -----------------------------------------------------------------------------
// StartTLS Support Callbacks
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  if (filter->readCallbacks() == nullptr) {
    return false;
  }

  return filter->readCallbacks()->startUpstreamSecureTransport();
}

// -----------------------------------------------------------------------------
// Network filter scheduler callbacks.
// -----------------------------------------------------------------------------

envoy_dynamic_module_type_network_filter_scheduler_module_ptr
envoy_dynamic_module_callback_network_filter_scheduler_new(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  Event::Dispatcher* dispatcher = filter->dispatcher();
  if (dispatcher == nullptr) {
    return nullptr;
  }
  return new DynamicModuleNetworkFilterScheduler(filter->weak_from_this(), *dispatcher);
}

void envoy_dynamic_module_callback_network_filter_scheduler_delete(
    envoy_dynamic_module_type_network_filter_scheduler_module_ptr scheduler_module_ptr) {
  delete static_cast<DynamicModuleNetworkFilterScheduler*>(scheduler_module_ptr);
}

void envoy_dynamic_module_callback_network_filter_scheduler_commit(
    envoy_dynamic_module_type_network_filter_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id) {
  auto* scheduler = static_cast<DynamicModuleNetworkFilterScheduler*>(scheduler_module_ptr);
  scheduler->commit(event_id);
}

envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr
envoy_dynamic_module_callback_network_filter_config_scheduler_new(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr filter_config_envoy_ptr) {
  auto* filter_config = static_cast<DynamicModuleNetworkFilterConfig*>(filter_config_envoy_ptr);
  return new DynamicModuleNetworkFilterConfigScheduler(filter_config->weak_from_this(),
                                                       filter_config->main_thread_dispatcher_);
}

void envoy_dynamic_module_callback_network_filter_config_scheduler_delete(
    envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr scheduler_module_ptr) {
  delete static_cast<DynamicModuleNetworkFilterConfigScheduler*>(scheduler_module_ptr);
}

void envoy_dynamic_module_callback_network_filter_config_scheduler_commit(
    envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id) {
  auto* scheduler = static_cast<DynamicModuleNetworkFilterConfigScheduler*>(scheduler_module_ptr);
  scheduler->commit(event_id);
}

// -----------------------------------------------------------------------------
// Misc ABI Callbacks
// -----------------------------------------------------------------------------

uint32_t envoy_dynamic_module_callback_network_filter_get_worker_index(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleNetworkFilter*>(filter_envoy_ptr);
  return filter->workerIndex();
}

} // extern "C"

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
