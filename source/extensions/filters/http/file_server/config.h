#pragma once

#include <string>

#include "envoy/extensions/filters/http/file_server/v3/file_server.pb.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

using ProtoFileServerConfig = envoy::extensions::filters::http::file_server::v3::FileServerConfig;

class FileServerFilterFactory
    : public Extensions::HttpFilters::Common::DualFactoryBase<ProtoFileServerConfig,
                                                              ProtoFileServerConfig> {
public:
  FileServerFilterFactory();

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const ProtoFileServerConfig& config,
                                    const std::string& stats_prefix, DualInfo info,
                                    Server::Configuration::ServerFactoryContext& context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(const ProtoFileServerConfig& config,
                                       Server::Configuration::ServerFactoryContext& context,
                                       ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
