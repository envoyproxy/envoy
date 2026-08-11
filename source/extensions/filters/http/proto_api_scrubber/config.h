#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.validate.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

class FilterFactoryCreator
    : public Envoy::Extensions::HttpFilters::Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig> {
public:
  FilterFactoryCreator();

private:
  absl::StatusOr<Envoy::Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig&
          proto_config,
      const std::string&, Envoy::Server::Configuration::FactoryContext&) override;

  absl::StatusOr<Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig&
          proto_config,
      const std::string&, Envoy::Server::Configuration::ServerFactoryContext&) override;

  // Shared factory creation used by both the downstream (FactoryContext) and route/vhost-level
  // (ServerFactoryContext) paths. Stats are scoped to the given scope.
  static absl::StatusOr<Envoy::Http::FilterFactoryCb> createFilterFactory(
      const envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig&
          proto_config,
      Envoy::Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope);
};
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
