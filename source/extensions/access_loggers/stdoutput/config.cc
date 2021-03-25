#include "extensions/access_loggers/stdoutput/config.h"

#include <memory>

#include "envoy/extensions/access_loggers/stdoutput/v3/stdoutput.pb.h"
#include "envoy/extensions/access_loggers/stdoutput/v3/stdoutput.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/common/logger.h"
#include "common/config/utility.h"
#include "common/formatter/substitution_format_string.h"
#include "common/formatter/substitution_formatter.h"
#include "common/protobuf/protobuf.h"

#include "extensions/access_loggers/common/file_access_log_impl.h"
#include "extensions/access_loggers/common/stream_access_log_common_impl.h"
#include "extensions/access_loggers/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

AccessLog::InstanceSharedPtr StdoutputAccessLogFactory::createAccessLogInstance(
    const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
    Server::Configuration::CommonFactoryContext& context) {
  return AccessLoggers::createStreamAccessLogInstance<
      envoy::extensions::access_loggers::stdoutput::v3::StdoutputAccessLog,
      Filesystem::DestinationType::Stdout>(config, std::move(filter), context);
}

ProtobufTypes::MessagePtr StdoutputAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::extensions::access_loggers::stdoutput::v3::StdoutputAccessLog()};
}

std::string StdoutputAccessLogFactory::name() const { return AccessLogNames::get().Stdoutput; }

/**
 * Static registration for the file access log. @see RegisterFactory.
 */
REGISTER_FACTORY(StdoutputAccessLogFactory,
                 Server::Configuration::AccessLogInstanceFactory){"envoy.stdoutput_access_log"};

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
