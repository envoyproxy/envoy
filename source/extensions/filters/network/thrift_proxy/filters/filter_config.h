#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/server/filter_config.h"

#include "common/common/macros.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/network/thrift_proxy/filters/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace ThriftFilters {

/**
 * Implemented by each Thrift filter and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedThriftFilterConfigFactory : public Envoy::Config::TypedFactory {
public:
  virtual ~NamedThriftFilterConfigFactory() = default;

  /**
   * Create a particular thrift filter factory implementation. If the implementation is unable to
   * produce a factory with the provided parameters, it should throw an EnvoyException in the case
   * of general error. The returned callback should always be initialized.
   * @param config supplies the configuration for the filter
   * @param stat_prefix prefix for stat logging
   * @param context supplies the filter's context.
   * @return FilterFactoryCb the factory creation function.
   */
  virtual FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.thrift_proxy.filters"; }
};

} // namespace ThriftFilters
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
