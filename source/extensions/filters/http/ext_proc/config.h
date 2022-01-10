#pragma once

#include <string>

#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class ExternalProcessingFilterConfig
    : public Common::FactoryBase<envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor,
                                 envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute> {

public:
  ExternalProcessingFilterConfig() : FactoryBase("envoy.filters.http.ext_proc") {}

private:
  static constexpr uint64_t DefaultMessageTimeoutMs = 200;

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
