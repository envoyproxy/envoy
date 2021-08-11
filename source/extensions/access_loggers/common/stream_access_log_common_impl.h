#pragma once

#include "envoy/server/access_log_config.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/access_loggers/common/file_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {

template <class T, Filesystem::DestinationType destination_type>
AccessLog::InstanceSharedPtr
createStreamAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                              Server::Configuration::CommonFactoryContext& context) {
  const auto& fal_config =
      MessageUtil::downcastAndValidate<const T&>(config, context.messageValidationVisitor());
  Formatter::FormatterPtr formatter;
  if (fal_config.access_log_format_case() == T::AccessLogFormatCase::kLogFormat) {
    formatter =
        Formatter::SubstitutionFormatStringUtils::fromProtoConfig(fal_config.log_format(), context);
  } else if (fal_config.access_log_format_case() ==
             T::AccessLogFormatCase::ACCESS_LOG_FORMAT_NOT_SET) {
    formatter = Formatter::SubstitutionFormatUtils::defaultSubstitutionFormatter();
  }
  Filesystem::FilePathAndType file_info{destination_type, ""};
  return std::make_shared<AccessLoggers::File::FileAccessLog>(
      file_info, std::move(filter), std::move(formatter), context.accessLogManager());
}

} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
