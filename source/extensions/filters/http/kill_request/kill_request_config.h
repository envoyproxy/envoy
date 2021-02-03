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

/**
 * Generic configuration for a kill request.
 */
class KillRequestConfig {
public:
  KillRequestConfig(
      const envoy::extensions::filters::http::kill_request::v3::KillRequest&
          kill_request);

  envoy::type::v3::FractionalPercent probability(
      const Http::RequestHeaderMap* request_headers) const {
    return provider_->probability(request_headers);
  }

private:
  // Abstract kill provider.
  class KillProvider {
   public:
    virtual ~KillProvider() = default;

    // Return what probability of requests kill should be applied to.
    virtual envoy::type::v3::FractionalPercent probability(
        const Http::RequestHeaderMap* request_headers) const PURE;
  };

  using KillProviderPtr = std::unique_ptr<KillProvider>;

  KillProviderPtr provider_;
};

using KillRequestConfigPtr = std::unique_ptr<KillRequestConfig>;
using KillRequestConfigSharedPtr = std::shared_ptr<KillRequestConfig>;

} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
