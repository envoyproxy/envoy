#pragma once

#include <memory>
#include <string>

#include "source/extensions/filters/http/grpc_field_extraction/filter_config.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.validate.h"
#include "envoy/http/filter.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {

class FilterFactoryCreator
    : public Envoy::Extensions::HttpFilters::Common::FactoryBase<
          envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig> {
public:
  FilterFactoryCreator() : FactoryBase("envoy.filters.http.grpc_field_extraction") {}

private:
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig&
          proto_config,
      const std::string&, Envoy::Server::Configuration::FactoryContext&) override;
};
} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction
