#include "extensions/access_loggers/file/config.h"

#include <memory>
#include <unordered_map>

#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/common/logger.h"
#include "common/formatter/substitution_format_string.h"
#include "common/formatter/substitution_formatter.h"
#include "common/protobuf/protobuf.h"

#include "extensions/access_loggers/file/file_access_log_impl.h"
#include "extensions/access_loggers/well_known_names.h"

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
      formatter = Formatter::SubstitutionFormatUtils::defaultSubstitutionFormatter();
    } else {
      envoy::config::core::v3::SubstitutionFormatString sff_config;
      sff_config.set_text_format(fal_config.format());
      formatter = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(sff_config);
    }
    break;
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::kJsonFormat:
    formatter = Formatter::SubstitutionFormatStringUtils::createJsonFormatter(
        fal_config.json_format(), false);
    break;
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
      kTypedJsonFormat: {
    envoy::config::core::v3::SubstitutionFormatString sff_config;
    *sff_config.mutable_json_format() = fal_config.typed_json_format();
    formatter = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(sff_config);
    break;
  }
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::kLogFormat:
    formatter = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(fal_config.log_format());
    break;
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
      ACCESS_LOG_FORMAT_NOT_SET:
    formatter = Formatter::SubstitutionFormatUtils::defaultSubstitutionFormatter();
    break;
  }

  return std::make_shared<FileAccessLog>(fal_config.path(), std::move(filter), std::move(formatter),
                                         context.accessLogManager());
}

ProtobufTypes::MessagePtr FileAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::extensions::access_loggers::file::v3::FileAccessLog()};
}

std::string FileAccessLogFactory::name() const { return AccessLogNames::get().File; }

/**
 * Static registration for the file access log. @see RegisterFactory.
 */
REGISTER_FACTORY(FileAccessLogFactory,
                 Server::Configuration::AccessLogInstanceFactory){"envoy.file_access_log"};

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
