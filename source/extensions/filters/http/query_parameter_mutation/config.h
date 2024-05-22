#pragma once

#include <string>

#include "envoy/extensions/filters/http/query_parameter_mutation/v3/config.pb.h"
#include "envoy/extensions/filters/http/query_parameter_mutation/v3/config.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/query_parameter_mutation/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace QueryParameterMutation {

class Factory : public Extensions::HttpFilters::Common::FactoryBase<FilterConfigProto> {
public:
  Factory() : FactoryBase("envoy.filters.http.query_parameter_mutation") {}

  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const FilterConfigProto&, const std::string&,
                                    Server::Configuration::FactoryContext&) override;
  Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfigTyped(const FilterConfigProto&,
                                       Server::Configuration::ServerFactoryContext&,
                                       ProtobufMessage::ValidationVisitor&) override;
};

} // namespace QueryParameterMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
