#pragma once

#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/server/filter_config.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Implemented for each AccessLog::Instance and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class AccessLogInstanceFactory {
public:
  virtual ~AccessLogInstanceFactory() {}

  /**
   * Create a particular AccessLog::Instance implementation from a config proto. If the
   * implementation is unable to produce a factory with the provided parameters, it should throw an
   * EnvoyException. The returned pointer should never be nullptr.
   * @param config the custom configuration for this access log type.
   * @param filter filter to determine whether a particular request should be logged. If no filter
   * was specified in the configuration, argument will be nullptr.
   * @param context general filter context through which persistent resources can be accessed.
   */
  virtual AccessLog::InstanceSharedPtr createAccessLogInstance(const Protobuf::Message& config,
                                                               AccessLog::FilterPtr&& filter,
                                                               FactoryContext& context) PURE;

  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message for v2. The config, which
   * arrives in an opaque google.protobuf.Struct message, will be converted to JSON and then parsed
   * into this empty proto.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * @return std::string the identifying name for a particular AccessLog::Instance implementation
   * produced by the factory.
   */
  virtual std::string name() const PURE;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
