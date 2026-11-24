#include "source/extensions/local_address_selectors/dynamic_modules/selector.h"

#include "source/common/common/thread.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace LocalAddressSelectors {
namespace DynamicModules {

namespace {

// Mirrors when parseBindConfig() adds IP_BIND_ADDRESS_NO_PORT to a bind config address.
bool bindAddressNoPortApplies(const Network::Address::InstanceConstSharedPtr& address) {
  return Runtime::runtimeFeatureEnabled(
             "envoy.reloadable_features.upstream_bind_config_fix_port_exhaustion") &&
         ENVOY_SOCKET_IP_BIND_ADDRESS_NO_PORT.hasValue() && address != nullptr &&
         address->ip() != nullptr && address->ip()->port() == 0;
}

Network::ConnectionSocket::OptionsSharedPtr
copyOptions(const Network::ConnectionSocket::OptionsSharedPtr& options) {
  return std::make_shared<Network::ConnectionSocket::Options>(
      options != nullptr ? *options : Network::ConnectionSocket::Options{});
}

} // namespace

absl::StatusOr<DynamicModuleLocalAddressSelectorConfigSharedPtr>
DynamicModuleLocalAddressSelectorConfig::create(Extensions::DynamicModules::DynamicModulePtr module,
                                                absl::string_view selector_name,
                                                absl::string_view selector_config,
                                                absl::string_view cluster_name) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  auto on_config_new = module->getFunctionPointer<OnLocalAddressSelectorConfigNewType>(
      "envoy_dynamic_module_on_local_address_selector_config_new");
  if (!on_config_new.ok()) {
    return absl::NotFoundError(on_config_new.status().message());
  }
  auto on_config_destroy = module->getFunctionPointer<OnLocalAddressSelectorConfigDestroyType>(
      "envoy_dynamic_module_on_local_address_selector_config_destroy");
  if (!on_config_destroy.ok()) {
    return absl::NotFoundError(on_config_destroy.status().message());
  }
  auto on_select = module->getFunctionPointer<OnLocalAddressSelectorSelectType>(
      "envoy_dynamic_module_on_local_address_selector_select");
  if (!on_select.ok()) {
    return absl::NotFoundError(on_select.status().message());
  }

  // The constructor is private, so std::make_shared cannot be used.
  std::shared_ptr<DynamicModuleLocalAddressSelectorConfig> config(
      new DynamicModuleLocalAddressSelectorConfig(std::move(module)));
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_select_ = on_select.value();

  config->in_module_config_ = (*on_config_new.value())(
      static_cast<void*>(config.get()), {selector_name.data(), selector_name.size()},
      {selector_config.data(), selector_config.size()}, {cluster_name.data(), cluster_name.size()});
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to initialize dynamic module local address selector config");
  }
  return config;
}

DynamicModuleLocalAddressSelectorConfig::DynamicModuleLocalAddressSelectorConfig(
    Extensions::DynamicModules::DynamicModulePtr module)
    : module_(std::move(module)) {}

DynamicModuleLocalAddressSelectorConfig::~DynamicModuleLocalAddressSelectorConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

bool FailConnectionSocketOption::setOption(
    Network::Socket&, envoy::config::core::v3::SocketOption::SocketState state) const {
  return state != envoy::config::core::v3::SocketOption::STATE_PREBIND;
}

DynamicModuleLocalAddressSelectorStats
generateDynamicModuleLocalAddressSelectorStats(Stats::Scope& scope) {
  return {ALL_DYNAMIC_MODULE_LOCAL_ADDRESS_SELECTOR_STATS(
      POOL_COUNTER_PREFIX(scope, "dynamic_module_local_address_selector."))};
}

DynamicModuleLocalAddressSelector::DynamicModuleLocalAddressSelector(
    DynamicModuleLocalAddressSelectorConfigSharedPtr config,
    Upstream::UpstreamLocalAddressSelectorConstSharedPtr default_selector,
    DynamicModuleLocalAddressSelectorStats stats)
    : config_(std::move(config)), default_selector_(std::move(default_selector)),
      stats_(std::move(stats)) {}

Upstream::UpstreamLocalAddress DynamicModuleLocalAddressSelector::getUpstreamLocalAddress(
    const Network::Address::InstanceConstSharedPtr& endpoint_address,
    const Network::ConnectionSocket::OptionsSharedPtr& socket_options,
    OptRef<const Network::TransportSocketOptions> transport_socket_options) const {
  Upstream::UpstreamLocalAddress default_address = default_selector_->getUpstreamLocalAddress(
      endpoint_address, socket_options, transport_socket_options);
  // Envoy applies no source address or socket options to non-IP connections.
  if (endpoint_address == nullptr || endpoint_address->ip() == nullptr) {
    return default_address;
  }

  const std::string& endpoint_ip = endpoint_address->ip()->addressAsString();
  LocalAddressSelection selection;
  if (!config_->on_select_(config_->in_module_config_, static_cast<void*>(&selection),
                           {endpoint_ip.data(), endpoint_ip.size()},
                           endpoint_address->ip()->port())) {
    stats_.failed_.inc();
    ENVOY_LOG(debug, "dynamic module local address selector failed the connection to {}",
              endpoint_address->asStringView());
    return failConnection(default_address);
  }
  if (selection.selected_address_ == nullptr) {
    stats_.default_.inc();
    return default_address;
  }
  stats_.selected_.inc();
  return useSelected(default_address, selection.selected_address_);
}

Upstream::UpstreamLocalAddress DynamicModuleLocalAddressSelector::useSelected(
    const Upstream::UpstreamLocalAddress& default_address,
    const Network::Address::InstanceConstSharedPtr& selected_address) {
  Network::ConnectionSocket::OptionsSharedPtr options =
      copyOptions(default_address.socket_options_);
  // The default address already carries IP_BIND_ADDRESS_NO_PORT when it applies to it.
  if (bindAddressNoPortApplies(selected_address) &&
      !bindAddressNoPortApplies(default_address.address_)) {
    Network::Socket::appendOptions(options, Network::SocketOptionFactory::buildBindAddressNoPort());
  }
  return {selected_address, options};
}

Upstream::UpstreamLocalAddress DynamicModuleLocalAddressSelector::failConnection(
    const Upstream::UpstreamLocalAddress& default_address) {
  Network::ConnectionSocket::OptionsSharedPtr options =
      copyOptions(default_address.socket_options_);
  options->push_back(std::make_shared<const FailConnectionSocketOption>());
  return {nullptr, options};
}

} // namespace DynamicModules
} // namespace LocalAddressSelectors
} // namespace Extensions
} // namespace Envoy
