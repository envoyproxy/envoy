#pragma once

#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/filter_config.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace AccessLog {

/**
 * Extension filter factory that reads from ExtensionFilter proto.
 */
template <class Context> class ExtensionFilterFactoryBase : public Config::TypedFactory {
public:
  ExtensionFilterFactoryBase()
      : category_(fmt::format("envoy.{}.access_loggers.extension_filters", Context::category())) {}

  ~ExtensionFilterFactoryBase() override = default;

  /**
   * Create a particular extension filter implementation from a config proto. If the
   * implementation is unable to produce a filter with the provided parameters, it should throw an
   * EnvoyException. The returned pointer should never be nullptr.
   * @param config supplies the custom configuration for this filter type.
   * @param context supplies the server factory context.
   * @return an instance of extension filter implementation from a config proto.
   */
  virtual FilterBasePtr<Context>
  createFilter(const Protobuf::Message& config,
               Server::Configuration::CommonFactoryContext& context) PURE;

  std::string category() const override { return category_; }

private:
  const std::string category_;
};

/**
 * Implemented for each AccessLog::Instance and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
template <class Context> class AccessLogInstanceFactoryBase : public Config::TypedFactory {
public:
  AccessLogInstanceFactoryBase()
      : category_(fmt::format("envoy.{}.access_loggers", Context::category())) {}

  ~AccessLogInstanceFactoryBase() override = default;

  /**
   * Create a particular AccessLog::Instance implementation from a config proto. If the
   * implementation is unable to produce a factory with the provided parameters, it should throw an
   * EnvoyException. The returned pointer should never be nullptr.
   * @param config the custom configuration for this access log type.
   * @param filter filter to determine whether a particular request should be logged. If no filter
   * was specified in the configuration, argument will be nullptr.
   * @param context access log context through which persistent resources can be accessed.
   */
  virtual AccessLog::InstanceBaseSharedPtr<Context>
  createAccessLogInstance(const Protobuf::Message& config,
                          AccessLog::FilterBasePtr<Context>&& filter,
                          Server::Configuration::ListenerAccessLogFactoryContext& context) PURE;

  /**
   * Create a particular AccessLog::Instance implementation from a config proto. If the
   * implementation is unable to produce a factory with the provided parameters, it should throw an
   * EnvoyException. The returned pointer should never be nullptr.
   * @param config the custom configuration for this access log type.
   * @param filter filter to determine whether a particular request should be logged. If no filter
   * was specified in the configuration, argument will be nullptr.
   * @param context general filter context through which persistent resources can be accessed.
   */
  virtual AccessLog::InstanceBaseSharedPtr<Context>
  createAccessLogInstance(const Protobuf::Message& config,
                          AccessLog::FilterBasePtr<Context>&& filter,
                          Server::Configuration::CommonFactoryContext& context) PURE;

  std::string category() const override { return category_; }

private:
  const std::string category_;
};

} // namespace AccessLog
} // namespace Envoy
