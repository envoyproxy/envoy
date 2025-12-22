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

bool envoy_dynamic_module_callback_listener_filter_get_buffer_slice(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_envoy_ptr* data_ptr_out, size_t* data_length_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  Network::ListenerFilterBuffer* buffer = filter->currentBuffer();

  if (buffer == nullptr) {
    *data_ptr_out = nullptr;
    *data_length_out = 0;
    return false;
  }

  auto raw_slice = buffer->rawSlice();
  *data_ptr_out = static_cast<char*>(const_cast<void*>(raw_slice.mem_));
  *data_length_out = raw_slice.len_;
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
    envoy_dynamic_module_type_const_buffer_envoy_ptr protocol, size_t length) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks != nullptr && protocol != nullptr && length > 0) {
    callbacks->socket().setDetectedTransportProtocol(absl::string_view(protocol, length));
  }
}

void envoy_dynamic_module_callback_listener_filter_set_requested_server_name(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_const_buffer_envoy_ptr name, size_t length) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks != nullptr && name != nullptr && length > 0) {
    callbacks->socket().setRequestedServerName(absl::string_view(name, length));
  }
}

void envoy_dynamic_module_callback_listener_filter_set_requested_application_protocols(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_const_buffer_envoy_ptr* protocols_ptr, size_t* protocols_length,
    size_t protocols_count) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr || protocols_ptr == nullptr || protocols_length == nullptr ||
      protocols_count == 0) {
    return;
  }

  std::vector<absl::string_view> protocols;
  protocols.reserve(protocols_count);
  for (size_t i = 0; i < protocols_count; ++i) {
    if (protocols_ptr[i] != nullptr && protocols_length[i] > 0) {
      protocols.emplace_back(protocols_ptr[i], protocols_length[i]);
    }
  }
  callbacks->socket().setRequestedApplicationProtocols(protocols);
}

void envoy_dynamic_module_callback_listener_filter_set_ja3_hash(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_const_buffer_envoy_ptr hash, size_t length) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks != nullptr && hash != nullptr && length > 0) {
    callbacks->socket().setJA3Hash(std::string(hash, length));
  }
}

void envoy_dynamic_module_callback_listener_filter_set_ja4_hash(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_const_buffer_envoy_ptr hash, size_t length) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks != nullptr && hash != nullptr && length > 0) {
    callbacks->socket().setJA4Hash(std::string(hash, length));
  }
}

size_t envoy_dynamic_module_callback_listener_filter_get_remote_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_envoy_ptr* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    *address_out = nullptr;
    *port_out = 0;
    return 0;
  }

  const auto& address = callbacks->socket().connectionInfoProvider().remoteAddress();
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

size_t envoy_dynamic_module_callback_listener_filter_get_local_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_envoy_ptr* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr) {
    *address_out = nullptr;
    *port_out = 0;
    return 0;
  }

  const auto& address = callbacks->socket().connectionInfoProvider().localAddress();
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

bool envoy_dynamic_module_callback_listener_filter_set_remote_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_const_buffer_envoy_ptr address, size_t address_length, uint32_t port,
    bool is_ipv6) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr || address == nullptr || address_length == 0) {
    return false;
  }

  std::string addr_str(address, address_length);
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
    envoy_dynamic_module_type_const_buffer_envoy_ptr address, size_t address_length, uint32_t port,
    bool is_ipv6) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr || address == nullptr || address_length == 0) {
    return false;
  }

  std::string addr_str(address, address_length);
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
    envoy_dynamic_module_type_const_buffer_envoy_ptr namespace_ptr, size_t namespace_size,
    envoy_dynamic_module_type_const_buffer_envoy_ptr key_ptr, size_t key_size,
    envoy_dynamic_module_type_const_buffer_envoy_ptr value_ptr, size_t value_size) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr || namespace_ptr == nullptr || key_ptr == nullptr ||
      value_ptr == nullptr) {
    return;
  }

  std::string ns(namespace_ptr, namespace_size);
  std::string key(key_ptr, key_size);
  std::string value(value_ptr, value_size);

  Protobuf::Struct metadata;
  auto& fields = *metadata.mutable_fields();
  fields[key].set_string_value(value);

  callbacks->setDynamicMetadata(ns, metadata);
}

void envoy_dynamic_module_callback_listener_filter_set_filter_state(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_const_buffer_envoy_ptr key_ptr, size_t key_size,
    envoy_dynamic_module_type_const_buffer_envoy_ptr value_ptr, size_t value_size) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr || key_ptr == nullptr || value_ptr == nullptr) {
    return;
  }

  std::string key(key_ptr, key_size);
  std::string value(value_ptr, value_size);

  callbacks->filterState().setData(key, std::make_shared<Router::StringAccessorImpl>(value),
                                   StreamInfo::FilterState::StateType::ReadOnly,
                                   StreamInfo::FilterState::LifeSpan::Connection);
}

size_t envoy_dynamic_module_callback_listener_filter_get_filter_state(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_const_buffer_envoy_ptr key_ptr, size_t key_size,
    envoy_dynamic_module_type_buffer_envoy_ptr* value_out) {
  auto* filter = static_cast<DynamicModuleListenerFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();

  if (callbacks == nullptr || key_ptr == nullptr) {
    *value_out = nullptr;
    return 0;
  }

  std::string key(key_ptr, key_size);
  const auto* accessor = callbacks->filterState().getDataReadOnly<Router::StringAccessor>(key);

  if (accessor == nullptr) {
    *value_out = nullptr;
    return 0;
  }

  // accessor->asString() returns a view to the string stored in the filter state.
  // The filter state is alive during the filter's lifetime, so this is safe.
  absl::string_view value = accessor->asString();
  *value_out = const_cast<char*>(value.data());
  return value.size();
}

} // extern "C"

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
