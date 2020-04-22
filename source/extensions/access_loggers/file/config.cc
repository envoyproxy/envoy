#include "extensions/access_loggers/file/config.h"

#include <memory>
#include <unordered_map>

#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/access_log/access_log_formatter.h"
#include "common/common/logger.h"
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
  AccessLog::FormatterPtr formatter;

  if (fal_config.access_log_format_case() == envoy::extensions::access_loggers::file::v3::
                                                 FileAccessLog::AccessLogFormatCase::kFormat ||
      fal_config.access_log_format_case() ==
          envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
              ACCESS_LOG_FORMAT_NOT_SET) {
    if (fal_config.format().empty()) {
      formatter = AccessLog::AccessLogFormatUtils::defaultAccessLogFormatter();
    } else {
      formatter = std::make_unique<AccessLog::FormatterImpl>(fal_config.format());
    }
  } else if (fal_config.access_log_format_case() ==
             envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
                 kJsonFormat) {
    auto json_format_map = this->convertJsonFormatToMap(fal_config.json_format());
    formatter = std::make_unique<AccessLog::JsonFormatterImpl>(json_format_map, false);
  } else if (fal_config.access_log_format_case() ==
             envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
                 kTypedJsonFormat) {
    auto json_format_map = this->convertJsonFormatToMap(fal_config.typed_json_format());
    formatter = std::make_unique<AccessLog::JsonFormatterImpl>(json_format_map, true);
  } else {
    throw EnvoyException(
        "Invalid access_log format provided. Only 'format', 'json_format', or 'typed_json_format' "
        "are supported.");
  }

  return std::make_shared<FileAccessLog>(fal_config.path(), std::move(filter), std::move(formatter),
                                         context.accessLogManager());
}

ProtobufTypes::MessagePtr FileAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::extensions::access_loggers::file::v3::FileAccessLog()};
}

std::string FileAccessLogFactory::name() const { return AccessLogNames::get().File; }

std::unordered_map<std::string, std::string>
FileAccessLogFactory::convertJsonFormatToMap(ProtobufWkt::Struct json_format) {
  std::unordered_map<std::string, std::string> output;
  for (const auto& pair : json_format.fields()) {
    if (pair.second.kind_case() != ProtobufWkt::Value::kStringValue) {
      throw EnvoyException("Only string values are supported in the JSON access log format.");
    }
    output.emplace(pair.first, pair.second.string_value());
  }
  return output;
}

/**
 * Static registration for the file access log. @see RegisterFactory.
 */
REGISTER_FACTORY(FileAccessLogFactory,
                 Server::Configuration::AccessLogInstanceFactory){"envoy.file_access_log"};

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
