#pragma once

#include "envoy/access_log/access_log_config.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/access_loggers/common/file_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {

template <class T, Filesystem::DestinationType destination_type>
AccessLog::InstanceSharedPtr
createStreamAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                              Server::Configuration::FactoryContext& context) {
  const auto& fal_config =
      MessageUtil::downcastAndValidate<const T&>(config, context.messageValidationVisitor());
  Formatter::FormatterPtr formatter;
  if (fal_config.access_log_format_case() == T::AccessLogFormatCase::kLogFormat) {
    formatter = THROW_OR_RETURN_VALUE(
        Formatter::SubstitutionFormatStringUtils::fromProtoConfig(fal_config.log_format(), context),
        Formatter::FormatterBasePtr<Formatter::HttpFormatterContext>);
  } else if (fal_config.access_log_format_case() ==
             T::AccessLogFormatCase::ACCESS_LOG_FORMAT_NOT_SET) {
    formatter = THROW_OR_RETURN_VALUE(
        Formatter::HttpSubstitutionFormatUtils::defaultSubstitutionFormatter(),
        Formatter::FormatterPtr);
  }
  Filesystem::FilePathAndType file_info{destination_type, ""};
  return std::make_shared<AccessLoggers::File::FileAccessLog>(
      file_info, std::move(filter), std::move(formatter),
      context.serverFactoryContext().accessLogManager());
}

} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
