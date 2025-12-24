#include "envoy/network/listen_socket.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/listener/dynamic_modules/filter.h"

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

void envoy_dynamic_module_callback_listener_filter_close_socket(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks != nullptr) {
    callbacks->socket().ioHandle().close();
  }
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

void envoy_dynamic_module_callback_listener_filter_set_filter_state(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr || key.ptr == nullptr || value.ptr == nullptr) {
    return;
  }

  std::string key_str(key.ptr, key.length);
  std::string value_str(value.ptr, value.length);

  callbacks->filterState().setData(key_str, std::make_shared<Router::StringAccessorImpl>(value_str),
                                   StreamInfo::FilterState::StateType::ReadOnly,
                                   StreamInfo::FilterState::LifeSpan::Connection);
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

} // extern "C"

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
