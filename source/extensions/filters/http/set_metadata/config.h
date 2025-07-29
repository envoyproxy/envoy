#pragma once

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"
#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.validate.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

/**
 * Config registration for the set-metadata filter. @see NamedHttpFilterConfigFactory.
 */
class SetMetadataConfig
    : public Common::CommonFactoryBase<envoy::extensions::filters::http::set_metadata::v3::Config>,
      public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  SetMetadataConfig() : CommonFactoryBase("envoy.filters.http.set_metadata") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<
            const envoy::extensions::filters::http::set_metadata::v3::Config&>(
            proto_config, context.messageValidationVisitor()),
        stats_prefix, context);
  }

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::set_metadata::v3::Config& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context);
};

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
