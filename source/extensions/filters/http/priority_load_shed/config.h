#pragma once

#include "envoy/extensions/filters/http/priority_load_shed/v3/priority_load_shed.pb.h"
#include "envoy/extensions/filters/http/priority_load_shed/v3/priority_load_shed.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PriorityLoadShed {

class PriorityLoadShedFilterFactory
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::priority_load_shed::v3::PriorityLoadShed> {
public:
  PriorityLoadShedFilterFactory()
      : ExceptionFreeFactoryBase("envoy.filters.http.priority_load_shed") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::priority_load_shed::v3::PriorityLoadShed&
          proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace PriorityLoadShed
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
