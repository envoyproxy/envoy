#include "server/config/access_log/file_access_log.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/access_log/access_log_formatter.h"
#include "common/access_log/access_log_impl.h"
#include "common/common/macros.h"
#include "common/config/well_known_names.h"
#include "common/protobuf/protobuf.h"

#include "api/filter/accesslog/accesslog.pb.validate.h"

namespace Envoy {
namespace Server {
namespace Configuration {

AccessLog::InstanceSharedPtr FileAccessLogFactory::createAccessLogInstance(
    const Protobuf::Message& config, AccessLog::FilterPtr&& filter, FactoryContext& context) {
  const auto& fal_config =
      MessageUtil::downcastAndValidate<const envoy::api::v2::filter::accesslog::FileAccessLog&>(
          config);
  AccessLog::FormatterPtr formatter;
  if (fal_config.format().empty()) {
    formatter = AccessLog::AccessLogFormatUtils::defaultAccessLogFormatter();
  } else {
    formatter.reset(new AccessLog::FormatterImpl(fal_config.format()));
  }
  return AccessLog::InstanceSharedPtr{new AccessLog::FileAccessLog(
      fal_config.path(), std::move(filter), std::move(formatter), context.accessLogManager())};
}

ProtobufTypes::MessagePtr FileAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{new envoy::api::v2::filter::accesslog::FileAccessLog()};
}

std::string FileAccessLogFactory::name() const { return Config::AccessLogNames::get().FILE; }

/**
 * Static registration for the file access log. @see RegisterFactory.
 */
static Registry::RegisterFactory<FileAccessLogFactory, AccessLogInstanceFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
