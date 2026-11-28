#pragma once

#include <memory>
#include <string>

#include "envoy/network/address.h"
#include "envoy/network/socket.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace LocalAddressSelectors {
namespace DynamicModules {

using OnLocalAddressSelectorConfigNewType =
    decltype(&envoy_dynamic_module_on_local_address_selector_config_new);
using OnLocalAddressSelectorConfigDestroyType =
    decltype(&envoy_dynamic_module_on_local_address_selector_config_destroy);
using OnLocalAddressSelectorSelectType =
    decltype(&envoy_dynamic_module_on_local_address_selector_select);

class DynamicModuleLocalAddressSelectorConfig;
using DynamicModuleLocalAddressSelectorConfigSharedPtr =
    std::shared_ptr<const DynamicModuleLocalAddressSelectorConfig>;

/**
 * Owns the loaded module and the in-module configuration of one cluster's selector. The in-module
 * configuration is destroyed before the module is unloaded.
 */
class DynamicModuleLocalAddressSelectorConfig {
public:
  /**
   * Resolves the selector symbols and creates the in-module configuration.
   * @return NotFound if the module lacks a selector symbol, or InvalidArgument if the module
   *   rejects the configuration.
   * @param module is the loaded dynamic module.
   * @param selector_name is passed to the module's config_new hook.
   * @param selector_config is passed to the module's config_new hook.
   * @param cluster_name is passed to the module's config_new hook. Empty for the bootstrap.
   */
  static absl::StatusOr<DynamicModuleLocalAddressSelectorConfigSharedPtr>
  create(Extensions::DynamicModules::DynamicModulePtr module, absl::string_view selector_name,
         absl::string_view selector_config, absl::string_view cluster_name);

  ~DynamicModuleLocalAddressSelectorConfig();

  OnLocalAddressSelectorSelectType on_select_{nullptr};
  envoy_dynamic_module_type_local_address_selector_config_module_ptr in_module_config_{nullptr};

private:
  explicit DynamicModuleLocalAddressSelectorConfig(
      Extensions::DynamicModules::DynamicModulePtr module);

  OnLocalAddressSelectorConfigDestroyType on_config_destroy_{nullptr};
  // Declared last so it is destroyed after every other member.
  Extensions::DynamicModules::DynamicModulePtr module_;
};

/**
 * The Envoy context of one call to the module's select hook. The module records its address here
 * with envoy_dynamic_module_callback_local_address_selector_set_address.
 */
struct LocalAddressSelection {
  Network::Address::InstanceConstSharedPtr selected_address_;
};

/**
 * A socket option that fails when Envoy applies the pre-bind options. The client connection then
 * closes itself before connecting, and the connection pool counts a connect failure. This is how a
 * selector fails a connection, since the selector interface has no error return.
 */
class FailConnectionSocketOption : public Network::Socket::Option {
public:
  bool setOption(Network::Socket& socket,
                 envoy::config::core::v3::SocketOption::SocketState state) const override;
  void hashKey(std::vector<uint8_t>&) const override {}
  std::optional<Details>
  getOptionDetails(const Network::Socket&,
                   envoy::config::core::v3::SocketOption::SocketState) const override {
    return std::nullopt;
  }
  bool isSupported() const override { return true; }
};

/**
 * All dynamic module local address selector stats. @see stats_macros.h
 */
#define ALL_DYNAMIC_MODULE_LOCAL_ADDRESS_SELECTOR_STATS(COUNTER)                                   \
  COUNTER(default)                                                                                 \
  COUNTER(failed)                                                                                  \
  COUNTER(selected)

/**
 * Counts the result of each selection, in the cluster's stats scope.
 */
struct DynamicModuleLocalAddressSelectorStats {
  ALL_DYNAMIC_MODULE_LOCAL_ADDRESS_SELECTOR_STATS(GENERATE_COUNTER_STRUCT)
};

DynamicModuleLocalAddressSelectorStats
generateDynamicModuleLocalAddressSelectorStats(Stats::Scope& scope);

/**
 * Local address selector that asks a dynamic module for the source address of each connection.
 * The module can also defer to the default selection over the bind config, or fail the connection.
 */
class DynamicModuleLocalAddressSelector : public Upstream::UpstreamLocalAddressSelector,
                                          public Logger::Loggable<Logger::Id::upstream> {
public:
  DynamicModuleLocalAddressSelector(
      DynamicModuleLocalAddressSelectorConfigSharedPtr config,
      Upstream::UpstreamLocalAddressSelectorConstSharedPtr default_selector,
      DynamicModuleLocalAddressSelectorStats stats);

  Upstream::UpstreamLocalAddress getUpstreamLocalAddress(
      const Network::Address::InstanceConstSharedPtr& endpoint_address,
      const Network::ConnectionSocket::OptionsSharedPtr& socket_options,
      OptRef<const Network::TransportSocketOptions> transport_socket_options) const override;

private:
  // Keeps the default selection's socket options so the bind config's options still apply.
  static Upstream::UpstreamLocalAddress
  useSelected(const Upstream::UpstreamLocalAddress& default_address,
              const Network::Address::InstanceConstSharedPtr& selected_address);
  static Upstream::UpstreamLocalAddress
  failConnection(const Upstream::UpstreamLocalAddress& default_address);

  const DynamicModuleLocalAddressSelectorConfigSharedPtr config_;
  const Upstream::UpstreamLocalAddressSelectorConstSharedPtr default_selector_;
  const DynamicModuleLocalAddressSelectorStats stats_;
};

} // namespace DynamicModules
} // namespace LocalAddressSelectors
} // namespace Extensions
} // namespace Envoy
