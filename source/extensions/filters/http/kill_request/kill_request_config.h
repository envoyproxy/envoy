#pragma once

#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.h"
#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {

/**
 * Config registration for KillRequestFilter. @see NamedHttpFilterConfigFactory.
 */
class KillRequestFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::kill_request::v3::KillRequest> {
public:
  KillRequestFilterFactory() : FactoryBase(HttpFilterNames::get().KillRequest) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::kill_request::v3::KillRequest& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
