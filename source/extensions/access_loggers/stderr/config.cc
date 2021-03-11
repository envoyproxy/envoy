#include "extensions/access_loggers/stderr/config.h"

#include <memory>

#include "envoy/extensions/access_loggers/stderr/v3/stderr.pb.h"
#include "envoy/extensions/access_loggers/stderr/v3/stderr.pb.validate.h"
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
StderrAccessLogFactory::createAccessLogInstance(const Protobuf::Message& config,
                                                AccessLog::FilterPtr&& filter,
                                                Server::Configuration::FactoryContext& context) {
  const auto& fal_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::stderr::v3::StdErrorAccessLog&>(
      config, context.messageValidationVisitor());
  Formatter::FormatterPtr formatter;
  if (fal_config.access_log_format_case() == envoy::extensions::access_loggers::stderr::v3::
                                                 StdErrorAccessLog::AccessLogFormatCase::kLogFormat) {
    formatter = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(fal_config.log_format(),
                                                                          context.api());
  }
  formatter = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(fal_config.log_format(),
                                                                        context.api());
  Filesystem::FilePathAndType file_info{Filesystem::DestinationType::Stderr, ""};
  return std::make_shared<FileAccessLog>(file_info, std::move(filter), std::move(formatter),
                                         context.accessLogManager());
}

ProtobufTypes::MessagePtr StderrAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::extensions::access_loggers::stderr::v3::StdErrorAccessLog()};
}

std::string StderrAccessLogFactory::name() const { return AccessLogNames::get().Stderr; }

/**
 * Static registration for the stderr access log. @see RegisterFactory.
 */
REGISTER_FACTORY(StderrAccessLogFactory,
                 Server::Configuration::AccessLogInstanceFactory){"envoy.stderr_access_log"};

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
