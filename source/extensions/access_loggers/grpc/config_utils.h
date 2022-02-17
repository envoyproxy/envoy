#pragma once

#include "envoy/server/filter_config.h"

#include "source/extensions/access_loggers/grpc/grpc_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

GrpcAccessLoggerCacheSharedPtr
getGrpcAccessLoggerCacheSingleton(Server::Configuration::CommonFactoryContext& context);

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
