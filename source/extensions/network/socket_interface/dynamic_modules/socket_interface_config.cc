#include "source/extensions/network/socket_interface/dynamic_modules/socket_interface_config.h"

#include "source/common/common/assert.h"
#include "source/common/stats/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

namespace {

DynamicModuleSocketInterfaceStats generateStats(absl::string_view socket_interface_name,
                                                Stats::Scope& scope) {
  const std::string prefix =
      absl::StrCat("dynamic_modules.socket_interface.",
                   Stats::Utility::sanitizeStatsName(socket_interface_name), ".");
  return {DYNAMIC_MODULE_SOCKET_INTERFACE_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
}

} // namespace

DynamicModuleSocketInterfaceConfig::DynamicModuleSocketInterfaceConfig(
    absl::string_view socket_interface_name, absl::string_view socket_interface_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& scope)
    : socket_interface_name_(socket_interface_name),
      socket_interface_config_(socket_interface_config),
      stats_(generateStats(socket_interface_name, scope)),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleSocketInterfaceConfig::~DynamicModuleSocketInterfaceConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

absl::StatusOr<DynamicModuleSocketInterfaceConfigSharedPtr> newDynamicModuleSocketInterfaceConfig(
    absl::string_view socket_interface_name, absl::string_view socket_interface_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& scope) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto config = std::make_shared<DynamicModuleSocketInterfaceConfig>(
      socket_interface_name, socket_interface_config, std::move(dynamic_module), scope);

#define RESOLVE_OR_RETURN(field, symbol)                                                           \
  {                                                                                                \
    auto result = config->dynamic_module_->getFunctionPointer<decltype(config->field)>(symbol);    \
    RETURN_IF_NOT_OK_REF(result.status());                                                         \
    config->field = result.value();                                                                \
  }

#define RESOLVE_OPTIONAL(field, symbol)                                                            \
  {                                                                                                \
    auto result = config->dynamic_module_->getFunctionPointer<decltype(config->field)>(symbol);    \
    if (result.ok()) {                                                                             \
      config->field = result.value();                                                              \
    }                                                                                              \
  }

  auto on_config_new = config->dynamic_module_->getFunctionPointer<OnSocketInterfaceConfigNewType>(
      "envoy_dynamic_module_on_socket_interface_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  RESOLVE_OR_RETURN(on_config_destroy_, "envoy_dynamic_module_on_socket_interface_config_destroy");
  RESOLVE_OR_RETURN(on_ip_family_supported_,
                    "envoy_dynamic_module_on_socket_interface_ip_family_supported");
  RESOLVE_OR_RETURN(on_socket_, "envoy_dynamic_module_on_socket_interface_socket");
  RESOLVE_OR_RETURN(on_socket_close_, "envoy_dynamic_module_on_socket_interface_socket_close");

  // The read and write hooks are all-or-none: a module that performs a socket's I/O provides both.
  RESOLVE_OPTIONAL(on_io_read_, "envoy_dynamic_module_on_socket_interface_io_read");
  RESOLVE_OPTIONAL(on_io_write_, "envoy_dynamic_module_on_socket_interface_io_write");
  if ((config->on_io_read_ == nullptr) != (config->on_io_write_ == nullptr)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to resolve symbol ",
        config->on_io_read_ == nullptr ? "envoy_dynamic_module_on_socket_interface_io_read"
                                       : "envoy_dynamic_module_on_socket_interface_io_write",
        " in dynamic module. The I/O hooks must be implemented together."));
  }
  if (config->implementsIoHooks()) {
    RESOLVE_OPTIONAL(on_io_shutdown_, "envoy_dynamic_module_on_socket_interface_io_shutdown");
  }
  RESOLVE_OPTIONAL(on_accept_, "envoy_dynamic_module_on_socket_interface_accept");

  // Sockets that delegate I/O or hand over connections need a readiness handle and a way to be
  // copied.
  if (config->implementsIoHooks() || config->implementsAccept()) {
    RESOLVE_OR_RETURN(on_set_socket_handle_,
                      "envoy_dynamic_module_on_socket_interface_set_socket_handle");
    RESOLVE_OR_RETURN(on_socket_duplicate_,
                      "envoy_dynamic_module_on_socket_interface_socket_duplicate");
  }

#undef RESOLVE_OPTIONAL
#undef RESOLVE_OR_RETURN

  envoy_dynamic_module_type_envoy_buffer name_buf = {
      .ptr = const_cast<char*>(config->socket_interface_name_.data()),
      .length = config->socket_interface_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {
      .ptr = const_cast<char*>(config->socket_interface_config_.data()),
      .length = config->socket_interface_config_.size()};
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buf, config_buf);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to initialize dynamic module socket interface config");
  }
  return config;
}

} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
