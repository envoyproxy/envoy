// NOLINT(namespace-envoy)

#include "source/common/network/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/local_address_selectors/dynamic_modules/selector.h"

extern "C" {

bool envoy_dynamic_module_callback_local_address_selector_set_address(
    envoy_dynamic_module_type_local_address_selector_envoy_ptr selector_envoy_ptr,
    envoy_dynamic_module_type_module_buffer address, uint32_t port) {
  using Envoy::Extensions::LocalAddressSelectors::DynamicModules::LocalAddressSelection;
  if (selector_envoy_ptr == nullptr || port > 65535 || address.ptr == nullptr) {
    return false;
  }
  auto parsed = Envoy::Network::Utility::parseInternetAddressNoThrow(
      std::string(address.ptr, address.length), static_cast<uint16_t>(port));
  if (parsed == nullptr) {
    return false;
  }
  static_cast<LocalAddressSelection*>(selector_envoy_ptr)->selected_address_ = std::move(parsed);
  return true;
}

} // extern "C"
