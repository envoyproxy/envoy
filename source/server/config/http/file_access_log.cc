#include "server/config/http/file_access_log.h"

#include "envoy/registry/registry.h"

#include "common/common/macros.h"
#include "common/http/access_log/access_log_formatter.h"
#include "common/http/access_log/access_log_impl.h"
#include "common/protobuf/protobuf.h"

#include "api/filter/http_connection_manager.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Http::AccessLog::InstanceSharedPtr
FileAccessLogFactory::createAccessLogInstance(const Protobuf::Message& config,
                                              Http::AccessLog::FilterPtr&& filter,
                                              AccessLog::AccessLogManager& log_manager) {
  const auto& fal_config = dynamic_cast<const envoy::api::v2::filter::FileAccessLog&>(config);
  Http::AccessLog::FormatterPtr formatter;
  if (fal_config.format().empty()) {
    formatter = Http::AccessLog::AccessLogFormatUtils::defaultAccessLogFormatter();
  } else {
    formatter.reset(new Http::AccessLog::FormatterImpl(fal_config.format()));
  }
  return Http::AccessLog::InstanceSharedPtr{new Http::AccessLog::FileAccessLog(
      fal_config.path(), std::move(filter), std::move(formatter), log_manager)};
}

ProtobufTypes::MessagePtr FileAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{new envoy::api::v2::filter::FileAccessLog()};
}

std::string FileAccessLogFactory::name() const { return "envoy.file_access_log"; }

/**
 * Static registration for the file access log. @see RegisterFactory.
 */
static Registry::RegisterFactory<FileAccessLogFactory, Http::AccessLog::AccessLogInstanceFactory>
    register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy