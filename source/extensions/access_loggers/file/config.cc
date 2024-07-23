#include "source/extensions/access_loggers/file/config.h"

#include <memory>

#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/logger.h"
#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/common/file_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

AccessLog::InstanceSharedPtr
FileAccessLogFactory::createAccessLogInstance(const Protobuf::Message& config,
                                              AccessLog::FilterPtr&& filter,
                                              Server::Configuration::FactoryContext& context) {
  const auto& fal_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::file::v3::FileAccessLog&>(
      config, context.messageValidationVisitor());
  Formatter::FormatterPtr formatter;

  switch (fal_config.access_log_format_case()) {
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::kFormat:
    if (fal_config.format().empty()) {
      formatter = Formatter::HttpSubstitutionFormatUtils::defaultSubstitutionFormatter();
    } else {
      envoy::config::core::v3::SubstitutionFormatString sff_config;
      sff_config.mutable_text_format_source()->set_inline_string(fal_config.format());
      formatter = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(sff_config, context);
    }
    break;
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::kJsonFormat:
    formatter = Formatter::SubstitutionFormatStringUtils::createJsonFormatter(
        fal_config.json_format(), false, false, false);
    break;
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
      kTypedJsonFormat: {
    envoy::config::core::v3::SubstitutionFormatString sff_config;
    *sff_config.mutable_json_format() = fal_config.typed_json_format();
    formatter = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(sff_config, context);
    break;
  }
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::kLogFormat:
    formatter =
        Formatter::SubstitutionFormatStringUtils::fromProtoConfig(fal_config.log_format(), context);
    break;
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
      ACCESS_LOG_FORMAT_NOT_SET:
    formatter = Formatter::HttpSubstitutionFormatUtils::defaultSubstitutionFormatter();
    break;
  }

  Filesystem::FilePathAndType file_info{Filesystem::DestinationType::File, fal_config.path()};
  return std::make_shared<FileAccessLog>(file_info, std::move(filter), std::move(formatter),
                                         context.serverFactoryContext().accessLogManager());
}

ProtobufTypes::MessagePtr FileAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::extensions::access_loggers::file::v3::FileAccessLog()};
}

std::string FileAccessLogFactory::name() const { return "envoy.access_loggers.file"; }

/**
 * Static registration for the file access log. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(FileAccessLogFactory, AccessLog::AccessLogInstanceFactory,
                        "envoy.file_access_log");

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
