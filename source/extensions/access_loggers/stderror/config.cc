#include "extensions/access_loggers/stderror/config.h"

#include <memory>

#include "envoy/extensions/access_loggers/stderror/v3/stderror.pb.h"
#include "envoy/extensions/access_loggers/stderror/v3/stderror.pb.validate.h"
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

AccessLog::InstanceSharedPtr StderrorAccessLogFactory::createAccessLogInstance(
    const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
    Server::Configuration::CommonFactoryContext& context) {
  return createStreamAccessLogInstance<
      envoy::extensions::access_loggers::stderror::v3::StdErrorAccessLog,
      Filesystem::DestinationType::Stderr>(config, std::move(filter), context);
}

ProtobufTypes::MessagePtr StderrorAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::extensions::access_loggers::stderror::v3::StdErrorAccessLog()};
}

std::string StderrorAccessLogFactory::name() const { return AccessLogNames::get().Stderror; }

/**
 * Static registration for the `stderror` access log. @see RegisterFactory.
 */
REGISTER_FACTORY(StderrorAccessLogFactory,
                 Server::Configuration::AccessLogInstanceFactory){"envoy.stderror_access_log"};

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy