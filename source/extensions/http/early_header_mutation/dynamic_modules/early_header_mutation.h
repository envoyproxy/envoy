#pragma once

#include <memory>
#include <string>

#include "envoy/http/early_header_mutation.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace DynamicModules {

// Type aliases for function pointers resolved from the module.
using OnEarlyHeaderMutationConfigNewType =
    decltype(&envoy_dynamic_module_on_early_header_mutation_config_new);
using OnEarlyHeaderMutationConfigDestroyType =
    decltype(&envoy_dynamic_module_on_early_header_mutation_config_destroy);
using OnEarlyHeaderMutationMutateType =
    decltype(&envoy_dynamic_module_on_early_header_mutation_mutate);

/**
 * Per-request context passed to the module as the
 * envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr. It bundles the mutable
 * request headers and the read-only stream info so the callbacks can read and rewrite the request.
 *
 * Stack-allocated for the duration of a single mutate() call on the worker thread owning the
 * request, so it holds references to objects that outlive the call and is never shared between
 * threads.
 */
struct EarlyHeaderMutationContext {
  ::Envoy::Http::RequestHeaderMap& headers;
  const StreamInfo::StreamInfo& stream_info;
};

/**
 * EarlyHeaderMutation implementation that delegates request header rewriting to a dynamic module.
 *
 * Unlike the formatter and the input matcher, exactly one instance is created per configured
 * extension entry, on the main thread, and it is owned by the HttpConnectionManagerConfig that is
 * shared by every worker thread for the lifetime of that config. This object therefore doubles as
 * the configuration object that config_envoy_ptr points at, and it holds no mutable state: every
 * member is written once before the object escapes the main thread, and mutate() is const.
 *
 * Note: symbol resolution and in-module config creation are done in the factory function
 * newDynamicModuleEarlyHeaderMutation() to provide graceful error handling. The constructor only
 * initializes basic members.
 */
class DynamicModuleEarlyHeaderMutation : public Envoy::Http::EarlyHeaderMutation,
                                         public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  DynamicModuleEarlyHeaderMutation(absl::string_view mutation_name,
                                   absl::string_view mutation_config,
                                   Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleEarlyHeaderMutation() override;

  // Envoy::Http::EarlyHeaderMutation
  bool mutate(::Envoy::Http::RequestHeaderMap& headers,
              const StreamInfo::StreamInfo& stream_info) const override;

  // The corresponding in-module early header mutation configuration.
  envoy_dynamic_module_type_early_header_mutation_config_module_ptr in_module_config_{nullptr};

  // The function pointers resolved from the module. Both are guaranteed non-nullptr after
  // newDynamicModuleEarlyHeaderMutation() succeeds.
  OnEarlyHeaderMutationConfigDestroyType on_config_destroy_{nullptr};
  OnEarlyHeaderMutationMutateType on_mutate_{nullptr};

private:
  // Prevent copy/move: the in-module configuration is destroyed exactly once by the destructor.
  DynamicModuleEarlyHeaderMutation(const DynamicModuleEarlyHeaderMutation&) = delete;
  DynamicModuleEarlyHeaderMutation& operator=(const DynamicModuleEarlyHeaderMutation&) = delete;

  friend absl::StatusOr<std::unique_ptr<DynamicModuleEarlyHeaderMutation>>
  newDynamicModuleEarlyHeaderMutation(absl::string_view mutation_name,
                                      absl::string_view mutation_config,
                                      Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  const std::string mutation_name_;
  const std::string mutation_config_;
  // Declared last so it is released only after the destructor body has run the in-module destroy
  // hook: the body runs before members are destroyed, and members are destroyed in reverse
  // declaration order, so the module is still loaded when the hook is called.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleEarlyHeaderMutationPtr = std::unique_ptr<DynamicModuleEarlyHeaderMutation>;

/**
 * Creates a new DynamicModuleEarlyHeaderMutation for the given configuration. Must be called on the
 * main thread.
 * @param mutation_name the name selecting an implementation inside the module.
 * @param mutation_config the configuration bytes for the early header mutation.
 * @param dynamic_module the dynamic module to use.
 * @return the new extension, a NotFoundError when a required ABI symbol is missing, or an
 * InvalidArgumentError when the in-module configuration failed to initialize. The caller maps the
 * status code onto the module_load_error and config_init_error counters respectively.
 */
absl::StatusOr<DynamicModuleEarlyHeaderMutationPtr>
newDynamicModuleEarlyHeaderMutation(absl::string_view mutation_name,
                                    absl::string_view mutation_config,
                                    Extensions::DynamicModules::DynamicModulePtr dynamic_module);

} // namespace DynamicModules
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
