#pragma once

#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/macros.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/network/dubbo_proxy/filters/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace DubboFilters {

/**
 * Implemented by each Dubbo filter and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedDubboFilterConfigFactory : public Envoy::Config::TypedFactory {
public:
  ~NamedDubboFilterConfigFactory() override = default;

  /**
   * Create a particular dubbo filter factory implementation. If the implementation is unable to
   * produce a factory with the provided parameters, it should throw an EnvoyException in the case
   * of general error. The returned callback should always be initialized.
   * @param config supplies the configuration for the filter
   * @param stat_prefix prefix for stat logging
   * @param context supplies the filter's context.
   * @return FilterFactoryCb the factory creation function.
   */
  virtual DubboFilters::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.dubbo_proxy.filters"; }
};

} // namespace DubboFilters
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
