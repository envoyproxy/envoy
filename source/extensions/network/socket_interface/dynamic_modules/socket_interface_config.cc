#include "source/extensions/network/socket_interface/dynamic_modules/socket_interface_config.h"

#include "source/common/common/assert.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

DynamicModuleSocketInterfaceConfig::DynamicModuleSocketInterfaceConfig(
    absl::string_view socket_interface_name, absl::string_view socket_interface_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : socket_interface_name_(socket_interface_name),
      socket_interface_config_(socket_interface_config),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleSocketInterfaceConfig::~DynamicModuleSocketInterfaceConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

absl::StatusOr<DynamicModuleSocketInterfaceConfigSharedPtr>
newDynamicModuleSocketInterfaceConfig(absl::string_view socket_interface_name,
                                      absl::string_view socket_interface_config,
                                      Extensions::DynamicModules::DynamicModulePtr dynamic_module) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto config = std::make_shared<DynamicModuleSocketInterfaceConfig>(
      socket_interface_name, socket_interface_config, std::move(dynamic_module));

#define RESOLVE_OR_RETURN(field, symbol)                                                           \
  {                                                                                                \
    auto result = config->dynamic_module_->getFunctionPointer<decltype(config->field)>(symbol);    \
    RETURN_IF_NOT_OK_REF(result.status());                                                         \
    config->field = result.value();                                                                \
  }

  auto on_config_new = config->dynamic_module_->getFunctionPointer<OnSocketInterfaceConfigNewType>(
      "envoy_dynamic_module_on_socket_interface_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  // Resolve required socket-creation symbols.
  RESOLVE_OR_RETURN(on_config_destroy_, "envoy_dynamic_module_on_socket_interface_config_destroy");
  RESOLVE_OR_RETURN(on_ip_family_supported_,
                    "envoy_dynamic_module_on_socket_interface_ip_family_supported");
  RESOLVE_OR_RETURN(on_socket_, "envoy_dynamic_module_on_socket_interface_socket");
  RESOLVE_OR_RETURN(on_socket_close_, "envoy_dynamic_module_on_socket_interface_socket_close");

  // Resolve the I/O hooks. They are all-or-none: a module that implements the data path must
  // provide read, write and the readiness-handle hook together. Absent hooks are valid: sockets
  // then use the standard fd-based handle.
#define RESOLVE_OPTIONAL(field, symbol)                                                            \
  {                                                                                                \
    auto result = config->dynamic_module_->getFunctionPointer<decltype(config->field)>(symbol);    \
    if (result.ok()) {                                                                             \
      config->field = result.value();                                                              \
    }                                                                                              \
  }

  RESOLVE_OPTIONAL(on_io_read_, "envoy_dynamic_module_on_socket_interface_io_read");
  RESOLVE_OPTIONAL(on_io_write_, "envoy_dynamic_module_on_socket_interface_io_write");
  RESOLVE_OPTIONAL(on_set_socket_handle_,
                   "envoy_dynamic_module_on_socket_interface_set_socket_handle");

  const bool io_read_present = config->on_io_read_ != nullptr;
  const bool io_write_present = config->on_io_write_ != nullptr;
  const bool set_handle_present = config->on_set_socket_handle_ != nullptr;
  if (io_read_present != io_write_present || io_read_present != set_handle_present) {
    std::string missing;
    if (!io_read_present) {
      absl::StrAppend(&missing, " envoy_dynamic_module_on_socket_interface_io_read");
    }
    if (!io_write_present) {
      absl::StrAppend(&missing, " envoy_dynamic_module_on_socket_interface_io_write");
    }
    if (!set_handle_present) {
      absl::StrAppend(&missing, " envoy_dynamic_module_on_socket_interface_set_socket_handle");
    }
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to resolve symbol(s)", missing,
                     " in dynamic module. The I/O hooks must be implemented together."));
  }
  if (io_read_present) {
    RESOLVE_OPTIONAL(on_io_shutdown_, "envoy_dynamic_module_on_socket_interface_io_shutdown");
  }

#undef RESOLVE_OPTIONAL

  // Resolve optional lifecycle hooks.
  {
    auto server_initialized =
        config->dynamic_module_->getFunctionPointer<OnSocketInterfaceOnServerInitializedType>(
            "envoy_dynamic_module_on_socket_interface_on_server_initialized");
    if (server_initialized.ok()) {
      config->on_server_initialized_ = server_initialized.value();
    }
  }
  {
    auto worker_thread_initialized =
        config->dynamic_module_->getFunctionPointer<OnSocketInterfaceOnWorkerThreadInitializedType>(
            "envoy_dynamic_module_on_socket_interface_on_worker_thread_initialized");
    if (worker_thread_initialized.ok()) {
      config->on_worker_thread_initialized_ = worker_thread_initialized.value();
    }
  }

#undef RESOLVE_OR_RETURN

  // Create the in-module configuration.
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
