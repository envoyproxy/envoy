#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.validate.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/grpc_field_extraction/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {

class FilterFactoryCreator
    : public Envoy::Extensions::HttpFilters::Common::FactoryBase<
          envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig> {
public:
  FilterFactoryCreator();

private:
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig&
          proto_config,
      const std::string&, Envoy::Server::Configuration::FactoryContext&) override;
};
} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
