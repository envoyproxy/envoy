#pragma once

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
class NamedThriftFilterConfigFactory {
public:
  virtual ~NamedThriftFilterConfigFactory() {}

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

  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message for v2. The filter
   *         config, which arrives in an opaque google.protobuf.Struct message, will be converted to
   *         JSON and then parsed into this empty proto.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * @return std::string the identifying name for a particular implementation of a thrift filter
   * produced by the factory.
   */
  virtual std::string name() PURE;
};

} // namespace ThriftFilters
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
