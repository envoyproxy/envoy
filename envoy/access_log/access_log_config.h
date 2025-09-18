#pragma once

#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/config/typed_config.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/server/filter_config.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace AccessLog {

/**
 * Extension filter factory that reads from ExtensionFilter proto.
 */
class ExtensionFilterFactory : public Config::TypedFactory {
public:
  /**
   * Create a particular extension filter implementation from a config proto. If the
   * implementation is unable to produce a filter with the provided parameters, it should throw an
   * EnvoyException. The returned pointer should never be nullptr.
   * @param config supplies the custom configuration for this filter type.
   * @param context supplies the factory context.
   * @return an instance of extension filter implementation from a config proto.
   */
  virtual FilterPtr createFilter(const envoy::config::accesslog::v3::ExtensionFilter& config,
                                 Server::Configuration::GenericFactoryContext& context) PURE;

  std::string category() const override { return "envoy.access_loggers.extension_filters"; }
};

/**
 * Implemented for each AccessLog::Instance and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class AccessLogInstanceFactory : public Config::TypedFactory {
public:
  /**
   * Create a particular AccessLog::Instance implementation from a config proto. If the
   * implementation is unable to produce a factory with the provided parameters, it should throw an
   * EnvoyException. The returned pointer should never be nullptr.
   * @param config the custom configuration for this access log type.
   * @param filter filter to determine whether a particular request should be logged. If no filter
   * was specified in the configuration, argument will be nullptr.
   * @param context access log context through which persistent resources can be accessed.
   * @param command_parsers vector of command parsers that provide by the caller to be used for
   * parsing custom substitution commands.
   */
  virtual AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                          Server::Configuration::GenericFactoryContext& context,
                          std::vector<Formatter::CommandParserPtr>&& command_parsers = {}) PURE;

  std::string category() const override { return "envoy.access_loggers"; }
};

} // namespace AccessLog
} // namespace Envoy
