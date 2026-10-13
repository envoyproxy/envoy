#include "source/extensions/filters/http/wasm/wasm_filter.h"

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

namespace {

Stats::Scope& statsScope(Server::Configuration::ServerFactoryContext& context,
                         Server::Configuration::ExtraFactoryContext& extra_context) {
  // Server scope for filters without a specified scope.
  if (!extra_context.scope.has_value()) {
    return context.scope();
  }

  // Downstream filters.
  if (!extra_context.is_upstream) {
    return *extra_context.scope;
  }

  // Upstream filters.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.upstream_wasm_filter_uses_root_scope")) {
    return context.serverScope();
  }
  return *extra_context.scope;
}

Init::Manager& initManager(Server::Configuration::ServerFactoryContext& context,
                           Server::Configuration::ExtraFactoryContext& extra_context) {
  return extra_context.init_manager.has_value() ? *extra_context.init_manager
                                                : context.initManager();
}

} // namespace

FilterConfig::FilterConfig(const envoy::extensions::filters::http::wasm::v3::Wasm& config,
                           Server::Configuration::ServerFactoryContext& context,
                           Server::Configuration::ExtraFactoryContext& extra_context)
    : Extensions::Common::Wasm::PluginConfig(config.config(), context,
                                             statsScope(context, extra_context),
                                             initManager(context, extra_context), false) {}

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
