#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/macros.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {

/**
 * Implemented by each UDP session filter and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedUdpSessionFilterConfigFactory : public Envoy::Config::TypedFactory {
public:
  ~NamedUdpSessionFilterConfigFactory() override = default;

  /**
   * Create a particular UDP session filter factory implementation. If the implementation is
   * unable to produce a factory with the provided parameters, it should throw an EnvoyException
   * in the case of general error. The returned callback should always be initialized.
   * @param config supplies the configuration for the filter
   * @param context supplies the filter's context.
   * @return FilterFactoryCb the factory creation function.
   */
  virtual FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.filters.udp.session"; }
};

} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
