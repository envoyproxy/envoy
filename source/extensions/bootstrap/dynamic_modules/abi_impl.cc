// NOLINT(namespace-envoy)
#include "source/common/http/message_impl.h"
#include "source/extensions/bootstrap/dynamic_modules/extension.h"
#include "source/extensions/dynamic_modules/abi.h"

extern "C" {

envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_bootstrap_extension_http_callout(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    uint64_t* callout_id_out, envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, uint64_t timeout_milliseconds) {
  auto* extension =
      static_cast<Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtension*>(
          extension_envoy_ptr);

  // Build the request message.
  Envoy::Http::RequestMessagePtr message = std::make_unique<Envoy::Http::RequestMessageImpl>();

  // Add headers.
  for (size_t i = 0; i < headers_size; i++) {
    const auto& header = headers[i];
    message->headers().addCopy(
        Envoy::Http::LowerCaseString(std::string(header.key_ptr, header.key_length)),
        std::string(header.value_ptr, header.value_length));
  }

  // Add body if present.
  if (body.length > 0 && body.ptr != nullptr) {
    message->body().add(body.ptr, body.length);
  }

  // Send the callout.
  return extension->sendHttpCallout(callout_id_out,
                                    std::string(cluster_name.ptr, cluster_name.length),
                                    std::move(message), timeout_milliseconds);
}

envoy_dynamic_module_type_bootstrap_extension_scheduler_module_ptr
envoy_dynamic_module_callback_bootstrap_extension_scheduler_new(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr) {
  auto* extension =
      static_cast<Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtension*>(
          extension_envoy_ptr);

  auto* scheduler =
      new Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtensionScheduler(
          extension, extension->mainThreadDispatcher());
  return static_cast<void*>(scheduler);
}

void envoy_dynamic_module_callback_bootstrap_extension_scheduler_delete(
    envoy_dynamic_module_type_bootstrap_extension_scheduler_module_ptr scheduler_module_ptr) {
  auto* scheduler = static_cast<
      Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtensionScheduler*>(
      scheduler_module_ptr);
  delete scheduler;
}

void envoy_dynamic_module_callback_bootstrap_extension_scheduler_commit(
    envoy_dynamic_module_type_bootstrap_extension_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id) {
  auto* scheduler = static_cast<
      Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtensionScheduler*>(
      scheduler_module_ptr);
  scheduler->commit(event_id);
}

} // extern "C"
