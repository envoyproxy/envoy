#pragma once

#include <functional>
#include <map>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Http {

class FilterChainFactoryCallbacks;

/**
 * This function is used to wrap the creation of an HTTP filter chain for new streams as they
 * come in. Filter factories create the function at configuration initialization time, and then
 * they are used at runtime.
 * @param callbacks supplies the callbacks for the stream to install filters to. Typically the
 * function will install a single filter, but it's technically possibly to install more than one
 * if desired.
 */
using FilterFactoryCb = std::function<void(FilterChainFactoryCallbacks& callbacks)>;

/**
 * Simple struct of additional contextual information of HTTP filter, e.g. filter config name
 * from configuration, etc.
 */
struct FilterContext {
  FilterContext() = default;
  FilterContext(absl::string_view name) : config_name(std::string(name)) {}
  // The name of the filter configuration that used to create related filter factory function.
  // This could be any legitimate non-empty string.
  std::string config_name;
};

/**
 * A FilterChainFactory is used by a connection manager to create an HTTP level filter chain when a
 * new stream is created on the connection (either locally or remotely). Typically it would be
 * implemented by a configuration engine that would install a set of filters that are able to
 * process an application scenario on top of a stream.
 */
class FilterChainFactory {
public:
  virtual ~FilterChainFactory() = default;

  /**
   * Called when a new HTTP stream is created on the connection.
   * @param callbacks supplies the callbacks that is used to create the filter chain.
   * @return whather a filter chain has been created.
   */
  virtual bool createFilterChain(FilterChainFactoryCallbacks& callbacks) const PURE;

  /**
   * Called when a new upgrade stream is created on the connection.
   * @param upgrade supplies the upgrade header from downstream
   * @param per_route_upgrade_map supplies the upgrade map, if any, for this route.
   * @param callbacks supplies the callbacks that is used to create the filter chain.
   * @return true if upgrades of this type are allowed and the filter chain has been created.
   *    returns false if this upgrade type is not configured, and no filter chain is created.
   */
  using UpgradeMap = std::map<std::string, bool>;
  virtual bool createUpgradeFilterChain(absl::string_view upgrade,
                                        const UpgradeMap* per_route_upgrade_map,
                                        FilterChainFactoryCallbacks& callbacks) const PURE;
};

} // namespace Http
} // namespace Envoy
