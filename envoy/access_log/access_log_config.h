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
  ExtensionFilterFactoryBase() : category_(categoryByType()) {}

  ~ExtensionFilterFactoryBase() override = default;

  /**
   * Create a particular extension filter implementation from a config proto. If the
   * implementation is unable to produce a filter with the provided parameters, it should throw an
   * EnvoyException. The returned pointer should never be nullptr.
   * @param config supplies the custom configuration for this filter type.
   * @param context supplies the factory context.
   * @return an instance of extension filter implementation from a config proto.
   */
  virtual FilterBasePtr<Context>
  createFilter(const envoy::config::accesslog::v3::ExtensionFilter& config,
               Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return category_; }

private:
  std::string categoryByType() {
    if constexpr (std::is_same_v<Context, Formatter::HttpFormatterContext>) {
      // This is a special case for the HTTP formatter context to ensure backwards compatibility.
      return "envoy.access_loggers.extension_filters";
    } else {
      return fmt::format("envoy.{}.access_loggers.extension_filters", Context::category());
    }
  }

  const std::string category_;
};

/**
 * Extension filter factory that reads from ExtensionFilter proto.
 */
using ExtensionFilterFactory = ExtensionFilterFactoryBase<Formatter::HttpFormatterContext>;

/**
 * Implemented for each AccessLog::Instance and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
template <class Context> class AccessLogInstanceFactoryBase : public Config::TypedFactory {
public:
  AccessLogInstanceFactoryBase() : category_(categoryByType()) {}

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
                          Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return category_; }

private:
  std::string categoryByType() {
    if constexpr (std::is_same_v<Context, Formatter::HttpFormatterContext>) {
      // This is a special case for the HTTP formatter context to ensure backwards compatibility.
      return "envoy.access_loggers";
    } else {
      return fmt::format("envoy.{}.access_loggers", Context::category());
    }
  }

  const std::string category_;
};

using AccessLogInstanceFactory = AccessLogInstanceFactoryBase<Formatter::HttpFormatterContext>;

} // namespace AccessLog
} // namespace Envoy
