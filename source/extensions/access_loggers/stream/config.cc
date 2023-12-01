#include "source/extensions/access_loggers/stream/config.h"

#include <memory>

#include "envoy/extensions/access_loggers/stream/v3/stream.pb.h"
#include "envoy/extensions/access_loggers/stream/v3/stream.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/logger.h"
#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/common/file_access_log_impl.h"
#include "source/extensions/access_loggers/common/stream_access_log_common_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

AccessLog::InstanceSharedPtr
StdoutAccessLogFactory::createAccessLogInstance(const Protobuf::Message& config,
                                                AccessLog::FilterPtr&& filter,
                                                Server::Configuration::FactoryContext& context) {
  return AccessLoggers::createStreamAccessLogInstance<
      envoy::extensions::access_loggers::stream::v3::StdoutAccessLog,
      Filesystem::DestinationType::Stdout>(config, std::move(filter), context);
}

ProtobufTypes::MessagePtr StdoutAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::extensions::access_loggers::stream::v3::StdoutAccessLog()};
}

std::string StdoutAccessLogFactory::name() const { return "envoy.access_loggers.stdout"; }

/**
 * Static registration for the file access log. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(StdoutAccessLogFactory, AccessLog::AccessLogInstanceFactory,
                        "envoy.stdout_access_log");

AccessLog::InstanceSharedPtr
StderrAccessLogFactory::createAccessLogInstance(const Protobuf::Message& config,
                                                AccessLog::FilterPtr&& filter,
                                                Server::Configuration::FactoryContext& context) {
  return createStreamAccessLogInstance<
      envoy::extensions::access_loggers::stream::v3::StderrAccessLog,
      Filesystem::DestinationType::Stderr>(config, std::move(filter), context);
}

ProtobufTypes::MessagePtr StderrAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::extensions::access_loggers::stream::v3::StderrAccessLog()};
}

std::string StderrAccessLogFactory::name() const { return "envoy.access_loggers.stderr"; }

/**
 * Static registration for the `stderr` access log. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(StderrAccessLogFactory, AccessLog::AccessLogInstanceFactory,
                        "envoy.stderr_access_log");

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
