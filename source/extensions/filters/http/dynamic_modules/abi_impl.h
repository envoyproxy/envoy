#pragma once

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/http/dynamic_modules/filter.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

static absl::optional<Stats::StatNameTagVector>
buildTagsForModuleMetric(DynamicModuleHttpFilter& filter,
                         const DynamicModuleHttpFilterConfig::ModuleMetricHandle& metric,
                         envoy_dynamic_module_type_module_str* label_values,
                         size_t label_values_length) {

  auto label_names = metric.getLabelNames();
  if (label_values_length == 0) {
    ASSERT(!label_names.has_value());
    return absl::nullopt;
  }

  ASSERT(label_names.has_value());
  ASSERT(label_values_length == label_names.value().get().size());
  Stats::StatNameTagVector tags;
  tags.reserve(label_values_length);
  for (size_t i = 0; i < label_values_length; i++) {
    absl::string_view label_value_view(label_values[i].ptr, label_values[i].length);
    auto label_value = filter.getStatNamePool().add(label_value_view);
    tags.push_back(Stats::StatNameTag(label_names.value().get()[i], label_value));
  }
  return tags;
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
