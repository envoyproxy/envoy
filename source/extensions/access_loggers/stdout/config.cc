#include "extensions/access_loggers/stdout/config.h"

#include <memory>

#include "envoy/extensions/access_loggers/stdout/v3/stdout.pb.h"
#include "envoy/extensions/access_loggers/stdout/v3/stdout.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/common/logger.h"
#include "common/config/utility.h"
#include "common/formatter/substitution_format_string.h"
#include "common/formatter/substitution_formatter.h"
#include "common/protobuf/protobuf.h"

#include "extensions/access_loggers/common/file_access_log_impl.h"
#include "extensions/access_loggers/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

AccessLog::InstanceSharedPtr
StdoutAccessLogFactory::createAccessLogInstance(const Protobuf::Message& config,
                                                AccessLog::FilterPtr&& filter,
                                                Server::Configuration::FactoryContext& context) {
  const auto& fal_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::stdout::v3::StdoutAccessLog&>(
      config, context.messageValidationVisitor());
  Formatter::FormatterPtr formatter;
  if (fal_config.access_log_format_case() == envoy::extensions::access_loggers::stdout::v3::
                                                 StdoutAccessLog::AccessLogFormatCase::kLogFormat) {
    formatter = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(fal_config.log_format(),
                                                                          context.api());
  }
  formatter = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(fal_config.log_format(),
                                                                        context.api());
  Filesystem::FilePathAndType file_info{Filesystem::DestinationType::Stdout, ""};
  return std::make_shared<FileAccessLog>(file_info, std::move(filter), std::move(formatter),
                                         context.accessLogManager());
}

ProtobufTypes::MessagePtr StdoutAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::extensions::access_loggers::stdout::v3::StdoutAccessLog()};
}

std::string StdoutAccessLogFactory::name() const { return AccessLogNames::get().Stdout; }

/**
 * Static registration for the file access log. @see RegisterFactory.
 */
REGISTER_FACTORY(StdoutAccessLogFactory,
                 Server::Configuration::AccessLogInstanceFactory){"envoy.stdout_access_log"};

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
