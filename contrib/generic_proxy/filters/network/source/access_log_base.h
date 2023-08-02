#pragma once

#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/config/utility.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

template <class FormatterContext> class AccessLogFilter {
public:
  virtual ~AccessLogFilter() = default;

  /**
   * @return bool whether the filter should be applied to the given stream.
   */
  virtual bool evaluate(const FormatterContext& context,
                        const StreamInfo::StreamInfo& info) const PURE;
};

template <class FormatterContext>
using AccessLogFilterPtr = std::unique_ptr<AccessLogFilter<FormatterContext>>;

template <class FormatterContext> class AccessLogFilterFactory : public Config::TypedFactory {
public:
  virtual AccessLogFilterPtr<FormatterContext>
  createFilter(const envoy::config::accesslog::v3::ExtensionFilter& config,
               Server::Configuration::CommonFactoryContext& context) PURE;

  std::string category() const override {
    return fmt::format("envoy.{}.access_loggers.extension_filters", FormatterContext::category());
  }
};

/**
 * Abstract access logger for requests and connections.
 */
template <class FormatterContext> class AccessLogInstance {
public:
  virtual ~AccessLogInstance() = default;

  /**
   * Log a completed request.
   * @param request_headers supplies the incoming request headers after filtering.
   * @param response_headers supplies response headers.
   * @param response_trailers supplies response trailers.
   * @param stream_info supplies additional information about the request not
   * contained in the request headers.
   * @param access_log_type supplies additional information about the type of the
   * log record, i.e the location in the code which recorded the log.
   */
  virtual void log(const FormatterContext& context, const StreamInfo::StreamInfo& info) PURE;
};

template <class FormatterContext>
using AccessLogInstanceSharedPtr = std::shared_ptr<AccessLogInstance<FormatterContext>>;

template <class FormatterContext> class AccessLogInstanceFactory : public Config::TypedFactory {
public:
  AccessLogInstanceFactory()
      : category_(fmt::format("envoy.{}.access_loggers", FormatterContext::category())) {}

  /**
   * Create a particular AccessLog::Instance implementation from a config proto. If the
   * implementation is unable to produce a factory with the provided parameters, it should throw an
   * EnvoyException. The returned pointer should never be nullptr.
   * @param config the custom configuration for this access log type.
   * @param filter filter to determine whether a particular request should be logged. If no filter
   * was specified in the configuration, argument will be nullptr.
   * @param context general filter context through which persistent resources can be accessed.
   */
  virtual AccessLogInstanceSharedPtr<FormatterContext>
  createAccessLogInstance(const Protobuf::Message& config,
                          AccessLogFilterPtr<FormatterContext>&& filter,
                          Server::Configuration::CommonFactoryContext& context) PURE;
  std::string category() const override { return category_; }

  /**
   * Create a particular AccessLog::Instance implementation from a config proto. If the
   * implementation is unable to produce a factory with the provided parameters, it should throw an
   * EnvoyException. The returned pointer should never be nullptr.
   */
  static AccessLogInstanceSharedPtr<FormatterContext>
  fromProto(const envoy::config::accesslog::v3::AccessLog& config,
            Server::Configuration::CommonFactoryContext& context) {

    auto& factory =
        Config::Utility::getAndCheckFactory<AccessLogInstanceFactory<FormatterContext>>(config);
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        config, context.messageValidationVisitor(), factory);

    // TODO(wbpcode): add filter support.
    return factory.createAccessLogInstance(*message, nullptr, context);
  }

private:
  const std::string category_;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
