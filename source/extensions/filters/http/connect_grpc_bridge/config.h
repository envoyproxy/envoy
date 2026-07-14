#pragma once

#include "envoy/extensions/filters/http/connect_grpc_bridge/v3/config.pb.h"
#include "envoy/extensions/filters/http/connect_grpc_bridge/v3/config.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectGrpcBridge {

class ConnectGrpcFilterConfigFactory
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::connect_grpc_bridge::v3::FilterConfig> {
public:
  ConnectGrpcFilterConfigFactory()
      : ExceptionFreeFactoryBase("envoy.filters.http.connect_grpc_bridge") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::connect_grpc_bridge::v3::FilterConfig& proto_config,
      const std::string&, Server::Configuration::FactoryContext&) override;

  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::connect_grpc_bridge::v3::FilterConfig& proto_config,
      const std::string&, Server::Configuration::ServerFactoryContext&) override;
};

} // namespace ConnectGrpcBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
