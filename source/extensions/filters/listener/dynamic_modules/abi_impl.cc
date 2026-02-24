#include <chrono>

#include "envoy/network/listen_socket.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/filters/listener/dynamic_modules/filter.h"
#include "source/extensions/filters/listener/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace ListenerFilters {

extern "C" {

bool envoy_dynamic_module_callback_listener_filter_get_buffer_chunk(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* chunk_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  Network::ListenerFilterBuffer* buffer = filter->currentBuffer();

  if (buffer == nullptr) {
    chunk_out->ptr = nullptr;
    chunk_out->length = 0;
    return false;
  }

  auto raw_slice = buffer->rawSlice();
  chunk_out->ptr = static_cast<char*>(const_cast<void*>(raw_slice.mem_));
  chunk_out->length = raw_slice.len_;
  return true;
}

bool envoy_dynamic_module_callback_listener_filter_drain_buffer(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, size_t length) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  Network::ListenerFilterBuffer* buffer = filter->currentBuffer();

  if (buffer == nullptr || length == 0) {
    return false;
  }

  buffer->drain(length);
  return true;
}

void envoy_dynamic_module_callback_listener_filter_set_detected_transport_protocol(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer protocol) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks != nullptr && protocol.ptr != nullptr && protocol.length > 0) {
    callbacks->socket().setDetectedTransportProtocol(
        absl::string_view(protocol.ptr, protocol.length));
  }
}

void envoy_dynamic_module_callback_listener_filter_set_requested_server_name(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks != nullptr && name.ptr != nullptr && name.length > 0) {
    callbacks->socket().setRequestedServerName(absl::string_view(name.ptr, name.length));
  }
}

void envoy_dynamic_module_callback_listener_filter_set_requested_application_protocols(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer* protocols, size_t protocols_count) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr || protocols == nullptr || protocols_count == 0) {
    return;
  }

  std::vector<absl::string_view> protocol_list;
  protocol_list.reserve(protocols_count);
  for (size_t i = 0; i < protocols_count; ++i) {
    if (protocols[i].ptr != nullptr && protocols[i].length > 0) {
      protocol_list.emplace_back(protocols[i].ptr, protocols[i].length);
    }
  }
  callbacks->socket().setRequestedApplicationProtocols(protocol_list);
}

void envoy_dynamic_module_callback_listener_filter_set_ja3_hash(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer hash) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks != nullptr && hash.ptr != nullptr && hash.length > 0) {
    callbacks->socket().setJA3Hash(std::string(hash.ptr, hash.length));
  }
}

void envoy_dynamic_module_callback_listener_filter_set_ja4_hash(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer hash) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks != nullptr && hash.ptr != nullptr && hash.length > 0) {
    callbacks->socket().setJA4Hash(std::string(hash.ptr, hash.length));
  }
}

bool envoy_dynamic_module_callback_listener_filter_get_requested_server_name(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    result_out->ptr = nullptr;
    result_out->length = 0;
    return false;
  }

  const absl::string_view sni = callbacks->socket().requestedServerName();
  if (sni.empty()) {
    result_out->ptr = nullptr;
    result_out->length = 0;
    return false;
  }

  result_out->ptr = const_cast<char*>(sni.data());
  result_out->length = sni.size();
  return true;
}

bool envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    result_out->ptr = nullptr;
    result_out->length = 0;
    return false;
  }

  const absl::string_view protocol = callbacks->socket().detectedTransportProtocol();
  if (protocol.empty()) {
    result_out->ptr = nullptr;
    result_out->length = 0;
    return false;
  }

  result_out->ptr = const_cast<char*>(protocol.data());
  result_out->length = protocol.size();
  return true;
}

size_t envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_size(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    return 0;
  }

  return callbacks->socket().requestedApplicationProtocols().size();
}

bool envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* protocols_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    return false;
  }

  const auto& protocols = callbacks->socket().requestedApplicationProtocols();
  // Populate the pre-allocated array. Module is responsible for allocating the correct size.
  for (size_t i = 0; i < protocols.size(); ++i) {
    protocols_out[i].ptr = const_cast<char*>(protocols[i].data());
    protocols_out[i].length = protocols[i].size();
  }
  return true;
}

bool envoy_dynamic_module_callback_listener_filter_get_ja3_hash(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    result_out->ptr = nullptr;
    result_out->length = 0;
    return false;
  }

  const absl::string_view hash = callbacks->socket().ja3Hash();
  if (hash.empty()) {
    result_out->ptr = nullptr;
    result_out->length = 0;
    return false;
  }

  result_out->ptr = const_cast<char*>(hash.data());
  result_out->length = hash.size();
  return true;
}

bool envoy_dynamic_module_callback_listener_filter_get_ja4_hash(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    result_out->ptr = nullptr;
    result_out->length = 0;
    return false;
  }

  const absl::string_view hash = callbacks->socket().ja4Hash();
  if (hash.empty()) {
    result_out->ptr = nullptr;
    result_out->length = 0;
    return false;
  }

  result_out->ptr = const_cast<char*>(hash.data());
  result_out->length = hash.size();
  return true;
}

bool envoy_dynamic_module_callback_listener_filter_is_ssl(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    return false;
  }

  const auto ssl = callbacks->socket().connectionInfoProvider().sslConnection();
  return ssl != nullptr;
}

size_t envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    return 0;
  }

  const auto ssl = callbacks->socket().connectionInfoProvider().sslConnection();
  if (!ssl) {
    return 0;
  }

  return ssl->uriSanPeerCertificate().size();
}

bool envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    return false;
  }

  const auto ssl = callbacks->socket().connectionInfoProvider().sslConnection();
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

size_t envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    return 0;
  }

  const auto ssl = callbacks->socket().connectionInfoProvider().sslConnection();
  if (!ssl) {
    return 0;
  }

  return ssl->dnsSansPeerCertificate().size();
}

bool envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    return false;
  }

  const auto ssl = callbacks->socket().connectionInfoProvider().sslConnection();
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

bool envoy_dynamic_module_callback_listener_filter_get_ssl_subject(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    result_out->ptr = nullptr;
    result_out->length = 0;
    return false;
  }

  const auto ssl = callbacks->socket().connectionInfoProvider().sslConnection();
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

bool envoy_dynamic_module_callback_listener_filter_get_remote_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    address_out->ptr = nullptr;
    address_out->length = 0;
    *port_out = 0;
    return false;
  }

  const auto& address = callbacks->socket().connectionInfoProvider().remoteAddress();
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

bool envoy_dynamic_module_callback_listener_filter_get_direct_remote_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    address_out->ptr = nullptr;
    address_out->length = 0;
    *port_out = 0;
    return false;
  }

  const auto& address = callbacks->socket().connectionInfoProvider().directRemoteAddress();
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

bool envoy_dynamic_module_callback_listener_filter_get_local_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    address_out->ptr = nullptr;
    address_out->length = 0;
    *port_out = 0;
    return false;
  }

  const auto& address = callbacks->socket().connectionInfoProvider().localAddress();
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

bool envoy_dynamic_module_callback_listener_filter_get_direct_local_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    address_out->ptr = nullptr;
    address_out->length = 0;
    *port_out = 0;
    return false;
  }

  const auto& address = callbacks->socket().connectionInfoProvider().directLocalAddress();
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

bool envoy_dynamic_module_callback_listener_filter_get_original_dst(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    address_out->ptr = nullptr;
    address_out->length = 0;
    *port_out = 0;
    return false;
  }

  if (callbacks->socket().addressType() != Network::Address::Type::Ip) {
    address_out->ptr = nullptr;
    address_out->length = 0;
    *port_out = 0;
    return false;
  }

  // Avoid calling getOriginalDst on an invalid handle.
  if (callbacks->socket().ioHandle().fdDoNotUse() < 0) {
    address_out->ptr = nullptr;
    address_out->length = 0;
    *port_out = 0;
    return false;
  }

  const auto original_dst = Network::Utility::getOriginalDst(callbacks->socket());
  if (original_dst == nullptr || original_dst->ip() == nullptr) {
    address_out->ptr = nullptr;
    address_out->length = 0;
    *port_out = 0;
    return false;
  }

  // Cache the address in the filter to ensure lifetime extends beyond this function.
  filter->cachedOriginalDst() = original_dst;

  const std::string& addr_str = original_dst->ip()->addressAsString();
  address_out->ptr = const_cast<char*>(addr_str.c_str());
  address_out->length = addr_str.size();
  *port_out = original_dst->ip()->port();
  return true;
}

envoy_dynamic_module_type_address_type
envoy_dynamic_module_callback_listener_filter_get_address_type(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    return envoy_dynamic_module_type_address_type_Unknown;
  }

  switch (callbacks->socket().addressType()) {
  case Network::Address::Type::Ip:
    return envoy_dynamic_module_type_address_type_Ip;
  case Network::Address::Type::Pipe:
    return envoy_dynamic_module_type_address_type_Pipe;
  case Network::Address::Type::EnvoyInternal:
    return envoy_dynamic_module_type_address_type_EnvoyInternal;
  }

  return envoy_dynamic_module_type_address_type_Unknown;
}

bool envoy_dynamic_module_callback_listener_filter_is_local_address_restored(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    return false;
  }

  return callbacks->socket().connectionInfoProvider().localAddressRestored();
}

bool envoy_dynamic_module_callback_listener_filter_set_remote_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer address, uint32_t port, bool is_ipv6) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr || address.ptr == nullptr || address.length == 0) {
    return false;
  }

  std::string addr_str(address.ptr, address.length);
  Network::Address::InstanceConstSharedPtr new_address;

  if (is_ipv6) {
    new_address = Network::Utility::parseInternetAddressAndPortNoThrow(
        absl::StrCat("[", addr_str, "]:", port));
  } else {
    new_address =
        Network::Utility::parseInternetAddressAndPortNoThrow(absl::StrCat(addr_str, ":", port));
  }

  if (new_address == nullptr) {
    return false;
  }

  callbacks->socket().connectionInfoProvider().setRemoteAddress(new_address);
  return true;
}

bool envoy_dynamic_module_callback_listener_filter_restore_local_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer address, uint32_t port, bool is_ipv6) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr || address.ptr == nullptr || address.length == 0) {
    return false;
  }

  std::string addr_str(address.ptr, address.length);
  Network::Address::InstanceConstSharedPtr new_address;

  if (is_ipv6) {
    new_address = Network::Utility::parseInternetAddressAndPortNoThrow(
        absl::StrCat("[", addr_str, "]:", port));
  } else {
    new_address =
        Network::Utility::parseInternetAddressAndPortNoThrow(absl::StrCat(addr_str, ":", port));
  }

  if (new_address == nullptr) {
    return false;
  }

  callbacks->socket().connectionInfoProvider().restoreLocalAddress(new_address);
  return true;
}

void envoy_dynamic_module_callback_listener_filter_continue_filter_chain(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, bool success) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks != nullptr) {
    callbacks->continueFilterChain(success);
  }
}

void envoy_dynamic_module_callback_listener_filter_use_original_dst(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, bool use_original_dst) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks != nullptr) {
    callbacks->useOriginalDst(use_original_dst);
  }
}

void envoy_dynamic_module_callback_listener_filter_close_socket(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer details) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr) {
    return;
  }
  if (details.ptr != nullptr && details.length > 0) {
    callbacks->streamInfo().setConnectionTerminationDetails(
        absl::string_view(details.ptr, details.length));
  }
  callbacks->socket().ioHandle().close();
}

int64_t envoy_dynamic_module_callback_listener_filter_write_to_socket(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr) {
    return -1;
  }
  if (data.ptr == nullptr || data.length == 0) {
    return -1;
  }
  Buffer::OwnedImpl buffer;
  buffer.add(data.ptr, data.length);
  Api::IoCallUint64Result result = callbacks->socket().ioHandle().write(buffer);
  if (result.ok()) {
    return static_cast<int64_t>(result.return_value_);
  }
  return -1;
}

int64_t envoy_dynamic_module_callback_listener_filter_get_socket_fd(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr) {
    return -1;
  }
  return callbacks->socket().ioHandle().fdDoNotUse();
}

bool envoy_dynamic_module_callback_listener_filter_set_socket_option_int(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, int64_t value) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr) {
    return false;
  }

  int int_value = static_cast<int>(value);
  auto result = callbacks->socket().setSocketOption(static_cast<int>(level), static_cast<int>(name),
                                                    &int_value, sizeof(int_value));
  return result.return_value_ == 0;
}

bool envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, envoy_dynamic_module_type_module_buffer value) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr || value.ptr == nullptr) {
    return false;
  }

  auto result =
      callbacks->socket().setSocketOption(static_cast<int>(level), static_cast<int>(name),
                                          value.ptr, static_cast<socklen_t>(value.length));
  return result.return_value_ == 0;
}

bool envoy_dynamic_module_callback_listener_filter_get_socket_option_int(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, int64_t* value_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr || value_out == nullptr) {
    return false;
  }

  int int_value = 0;
  socklen_t optlen = sizeof(int_value);
  auto result = callbacks->socket().getSocketOption(static_cast<int>(level), static_cast<int>(name),
                                                    &int_value, &optlen);
  if (result.return_value_ != 0) {
    return false;
  }

  *value_out = int_value;
  return true;
}

bool envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, char* value_out, size_t value_size, size_t* actual_size_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr || value_out == nullptr || actual_size_out == nullptr) {
    return false;
  }

  socklen_t optlen = static_cast<socklen_t>(value_size);
  auto result = callbacks->socket().getSocketOption(static_cast<int>(level), static_cast<int>(name),
                                                    value_out, &optlen);
  if (result.return_value_ != 0) {
    return false;
  }

  *actual_size_out = optlen;
  return true;
}

void envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr || filter_namespace.ptr == nullptr || key.ptr == nullptr ||
      value.ptr == nullptr) {
    return;
  }

  std::string ns(filter_namespace.ptr, filter_namespace.length);
  std::string key_str(key.ptr, key.length);
  std::string value_str(value.ptr, value.length);

  Protobuf::Struct metadata;
  auto& fields = *metadata.mutable_fields();
  fields[key_str].set_string_value(value_str);

  callbacks->setDynamicMetadata(ns, metadata);
}

bool envoy_dynamic_module_callback_listener_filter_set_filter_state(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr || key.ptr == nullptr || value.ptr == nullptr) {
    return false;
  }

  std::string key_str(key.ptr, key.length);
  std::string value_str(value.ptr, value.length);

  // TODO(wbpcode): check whether the key already exists and whether overwriting is allowed.
  callbacks->filterState().setData(key_str, std::make_shared<Router::StringAccessorImpl>(value_str),
                                   StreamInfo::FilterState::StateType::ReadOnly,
                                   StreamInfo::FilterState::LifeSpan::Connection);
  return true;
}

bool envoy_dynamic_module_callback_listener_filter_get_filter_state(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr || key.ptr == nullptr) {
    value_out->ptr = nullptr;
    value_out->length = 0;
    return false;
  }

  std::string key_str(key.ptr, key.length);
  const auto* accessor = callbacks->filterState().getDataReadOnly<Router::StringAccessor>(key_str);

  if (accessor == nullptr) {
    value_out->ptr = nullptr;
    value_out->length = 0;
    return false;
  }

  // accessor->asString() returns a view to the string stored in the filter state.
  // The filter state is alive during the filter's lifetime, so this is safe.
  absl::string_view value = accessor->asString();
  value_out->ptr = const_cast<char*>(value.data());
  value_out->length = value.size();
  return true;
}

void envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_reason(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer reason) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr || reason.ptr == nullptr || reason.length == 0) {
    return;
  }

  callbacks->streamInfo().setDownstreamTransportFailureReason(
      absl::string_view(reason.ptr, reason.length));
}

uint64_t envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr) {
    return 0;
  }

  const auto start_time = callbacks->streamInfo().startTime();
  const auto duration = start_time.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

bool envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr || filter_namespace.ptr == nullptr || key.ptr == nullptr) {
    value_out->ptr = nullptr;
    value_out->length = 0;
    return false;
  }

  std::string ns(filter_namespace.ptr, filter_namespace.length);
  std::string key_str(key.ptr, key.length);

  const auto& metadata = callbacks->dynamicMetadata();
  const auto& fields = metadata.filter_metadata();
  auto ns_it = fields.find(ns);

  if (ns_it == fields.end()) {
    value_out->ptr = nullptr;
    value_out->length = 0;
    return false;
  }

  const auto& ns_fields = ns_it->second.fields();
  auto field_it = ns_fields.find(key_str);

  if (field_it == ns_fields.end() ||
      field_it->second.kind_case() != Protobuf::Value::kStringValue) {
    value_out->ptr = nullptr;
    value_out->length = 0;
    return false;
  }

  const std::string& value = field_it->second.string_value();
  value_out->ptr = const_cast<char*>(value.c_str());
  value_out->length = value.size();
  return true;
}

void envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr || filter_namespace.ptr == nullptr || key.ptr == nullptr ||
      value.ptr == nullptr) {
    // TODO(wbpcode): These should never happen and may be converted to asserts.
    return;
  }

  std::string ns(filter_namespace.ptr, filter_namespace.length);
  std::string key_str(key.ptr, key.length);
  std::string value_str(value.ptr, value.length);

  Protobuf::Struct metadata;
  auto& fields = *metadata.mutable_fields();
  fields[key_str].set_string_value(value_str);

  callbacks->setDynamicMetadata(ns, metadata);
}

size_t envoy_dynamic_module_callback_listener_filter_max_read_bytes(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  return filter->maxReadBytes();
}

// -----------------------------------------------------------------------------
// Metrics Callbacks
// -----------------------------------------------------------------------------

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_config_define_counter(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* counter_id_ptr) {
  auto* config = static_cast<DynamicModuleListenerFilterConfig*>(config_envoy_ptr);
  Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Stats::Counter& c = Stats::Utility::counterFromStatNames(*config->stats_scope_, {main_stat_name});
  *counter_id_ptr = config->addCounter({c});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_increment_counter(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto counter = filter->getFilterConfig().getCounterById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  counter->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_config_define_gauge(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* gauge_id_ptr) {
  auto* config = static_cast<DynamicModuleListenerFilterConfig*>(config_envoy_ptr);
  Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Stats::Gauge& g = Stats::Utility::gaugeFromStatNames(*config->stats_scope_, {main_stat_name},
                                                       Stats::Gauge::ImportMode::Accumulate);
  *gauge_id_ptr = config->addGauge({g});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_listener_filter_set_gauge(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->set(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_increment_gauge(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_decrement_gauge(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->sub(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_config_define_histogram(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* histogram_id_ptr) {
  auto* config = static_cast<DynamicModuleListenerFilterConfig*>(config_envoy_ptr);
  Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Stats::Histogram& h = Stats::Utility::histogramFromStatNames(
      *config->stats_scope_, {main_stat_name}, Stats::Histogram::Unit::Unspecified);
  *histogram_id_ptr = config->addHistogram({h});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_record_histogram_value(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto histogram = filter->getFilterConfig().getHistogramById(id);
  if (!histogram.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  histogram->recordValue(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

// -----------------------------------------------------------------------------
// HTTP Callout Callbacks
// -----------------------------------------------------------------------------

envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_listener_filter_http_callout(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, uint64_t* callout_id_out,
    envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, uint64_t timeout_milliseconds) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);

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

// -----------------------------------------------------------------------------
// Scheduler Callbacks
// -----------------------------------------------------------------------------

envoy_dynamic_module_type_listener_filter_scheduler_module_ptr
envoy_dynamic_module_callback_listener_filter_scheduler_new(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  Event::Dispatcher* dispatcher = filter->dispatcher();
  if (dispatcher == nullptr) {
    return nullptr;
  }
  return new DynamicModuleListenerFilterScheduler(filter->weak_from_this(), *dispatcher);
}

void envoy_dynamic_module_callback_listener_filter_scheduler_delete(
    envoy_dynamic_module_type_listener_filter_scheduler_module_ptr scheduler_module_ptr) {
  delete static_cast<DynamicModuleListenerFilterScheduler*>(scheduler_module_ptr);
}

void envoy_dynamic_module_callback_listener_filter_scheduler_commit(
    envoy_dynamic_module_type_listener_filter_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id) {
  auto* scheduler = static_cast<DynamicModuleListenerFilterScheduler*>(scheduler_module_ptr);
  scheduler->commit(event_id);
}

envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr
envoy_dynamic_module_callback_listener_filter_config_scheduler_new(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr filter_config_envoy_ptr) {
  auto* filter_config = static_cast<DynamicModuleListenerFilterConfig*>(filter_config_envoy_ptr);
  return new DynamicModuleListenerFilterConfigScheduler(filter_config->weak_from_this(),
                                                        filter_config->main_thread_dispatcher_);
}

void envoy_dynamic_module_callback_listener_filter_config_scheduler_delete(
    envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr scheduler_module_ptr) {
  delete static_cast<DynamicModuleListenerFilterConfigScheduler*>(scheduler_module_ptr);
}

void envoy_dynamic_module_callback_listener_filter_config_scheduler_commit(
    envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id) {
  auto* scheduler = static_cast<DynamicModuleListenerFilterConfigScheduler*>(scheduler_module_ptr);
  scheduler->commit(event_id);
}

// -----------------------------------------------------------------------------
// Misc ABI Callbacks
// -----------------------------------------------------------------------------

uint32_t envoy_dynamic_module_callback_listener_filter_get_worker_index(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  return filter->workerIndex();
}

} // extern "C"

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
