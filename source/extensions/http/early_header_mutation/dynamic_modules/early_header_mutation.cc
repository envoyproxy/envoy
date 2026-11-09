#include "source/extensions/http/early_header_mutation/dynamic_modules/early_header_mutation.h"

#include <string>
#include <utility>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace DynamicModules {

DynamicModuleEarlyHeaderMutation::DynamicModuleEarlyHeaderMutation(
    absl::string_view mutation_name, absl::string_view mutation_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : mutation_name_(mutation_name), mutation_config_(mutation_config),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleEarlyHeaderMutation::~DynamicModuleEarlyHeaderMutation() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

bool DynamicModuleEarlyHeaderMutation::mutate(::Envoy::Http::RequestHeaderMap& headers,
                                              const StreamInfo::StreamInfo& stream_info) const {
  EarlyHeaderMutationContext context{headers, stream_info};
  return on_mutate_(in_module_config_, static_cast<void*>(&context));
}

absl::StatusOr<DynamicModuleEarlyHeaderMutationPtr>
newDynamicModuleEarlyHeaderMutation(absl::string_view mutation_name,
                                    absl::string_view mutation_config,
                                    Extensions::DynamicModules::DynamicModulePtr dynamic_module) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  // A missing ABI symbol is a module-level problem, so it is reported as NotFound and the caller
  // counts it as module_load_error rather than config_init_error.
  auto on_config_new = dynamic_module->getFunctionPointer<OnEarlyHeaderMutationConfigNewType>(
      "envoy_dynamic_module_on_early_header_mutation_config_new");
  if (!on_config_new.ok()) {
    return absl::NotFoundError(on_config_new.status().message());
  }

  auto on_config_destroy =
      dynamic_module->getFunctionPointer<OnEarlyHeaderMutationConfigDestroyType>(
          "envoy_dynamic_module_on_early_header_mutation_config_destroy");
  if (!on_config_destroy.ok()) {
    return absl::NotFoundError(on_config_destroy.status().message());
  }

  auto on_mutate = dynamic_module->getFunctionPointer<OnEarlyHeaderMutationMutateType>(
      "envoy_dynamic_module_on_early_header_mutation_mutate");
  if (!on_mutate.ok()) {
    return absl::NotFoundError(on_mutate.status().message());
  }

  auto extension = std::make_unique<DynamicModuleEarlyHeaderMutation>(
      mutation_name, mutation_config, std::move(dynamic_module));
  extension->on_config_destroy_ = on_config_destroy.value();
  extension->on_mutate_ = on_mutate.value();

  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = extension->mutation_name_.data(),
                                                     .length = extension->mutation_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {
      .ptr = extension->mutation_config_.data(), .length = extension->mutation_config_.size()};
  extension->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(extension.get()), name_buf, config_buf);

  if (extension->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to initialize dynamic module early header mutation config");
  }
  return extension;
}

} // namespace DynamicModules
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
