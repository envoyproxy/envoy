#include "extensions/access_loggers/file/config.h"

#include "envoy/config/filter/accesslog/v2/accesslog.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/access_log/access_log_formatter.h"
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
  const auto& fal_config =
      MessageUtil::downcastAndValidate<const envoy::config::filter::accesslog::v2::FileAccessLog&>(
          config);
  AccessLog::FormatterPtr formatter;
  if (fal_config.format().empty()) {
    formatter = AccessLog::AccessLogFormatUtils::defaultAccessLogFormatter();
  } else {
    formatter.reset(new AccessLog::FormatterImpl(fal_config.format()));
  }
  return std::make_shared<FileAccessLog>(fal_config.path(), std::move(filter), std::move(formatter),
                                         context.accessLogManager());
}

ProtobufTypes::MessagePtr FileAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{new envoy::config::filter::accesslog::v2::FileAccessLog()};
}

std::string FileAccessLogFactory::name() const { return AccessLogNames::get().FILE; }

/**
 * Static registration for the file access log. @see RegisterFactory.
 */
static Registry::RegisterFactory<FileAccessLogFactory,
                                 Server::Configuration::AccessLogInstanceFactory>
    register_;

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
