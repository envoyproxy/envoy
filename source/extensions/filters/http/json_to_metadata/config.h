#pragma once

#include <string>

#include "envoy/extensions/filters/http/json_to_metadata/v3/json_to_metadata.pb.h"
#include "envoy/extensions/filters/http/json_to_metadata/v3/json_to_metadata.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonToMetadata {

class JsonToMetadataConfig
    : public Extensions::HttpFilters::Common::FactoryBase<
          envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata> {
public:
  JsonToMetadataConfig();

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata&,
      const std::string&, Server::Configuration::FactoryContext&) override;
};

} // namespace JsonToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
