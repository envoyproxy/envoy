#pragma once

#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using FilterConfig = envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter;

class DynamicModuleConfigFactory
    : public Extensions::HttpFilters::Common::DualFactoryBase<FilterConfig> {
public:
  DynamicModuleConfigFactory() : DualFactoryBase("envoy.extensions.filters.http.dynamic_modules") {}
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const FilterConfig& raw_config, const std::string&, DualInfo,
                                    Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new FilterConfig()};
  }

  std::string name() const override { return "envoy.extensions.filters.http.dynamic_modules"; }
};
using UpstreamDynamicModuleConfigFactory = DynamicModuleConfigFactory;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
