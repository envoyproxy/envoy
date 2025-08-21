#pragma once

#include <string>

#include "envoy/extensions/filters/http/thrift_to_metadata/v3/thrift_to_metadata.pb.h"
#include "envoy/extensions/filters/http/thrift_to_metadata/v3/thrift_to_metadata.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ThriftToMetadata {

class ThriftToMetadataConfig
    : public Extensions::HttpFilters::Common::FactoryBase<
          envoy::extensions::filters::http::thrift_to_metadata::v3::ThriftToMetadata> {
public:
  ThriftToMetadataConfig();

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::thrift_to_metadata::v3::ThriftToMetadata&,
      const std::string&, Server::Configuration::FactoryContext&) override;
};

} // namespace ThriftToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
