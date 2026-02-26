#pragma once

#include <memory>

#include "envoy/stats/stats.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

template <class StatType> class StatMatchingDataImpl : public StatMatchingData {
public:
  explicit StatMatchingDataImpl(const StatType& metric) : metric_(metric), name_(metric.name()) {
    for (const auto& tag : metric.tags()) {
      tag_map_[tag.name_] = tag.value_;
    }
  }

  static std::string name() { return "stat_matching_data_impl"; }

  std::string fullName() const override { return name_; }

  std::string tagValue(absl::string_view name) const override {
    auto it = tag_map_.find(name);
    if (it != tag_map_.end()) {
      return std::string(it->second);
    }
    return {};
  }

private:
  const StatType& metric_;
  std::string name_;
  // Map from tag name to tag value.
  absl::flat_hash_map<absl::string_view, absl::string_view> tag_map_;
};

} // namespace Stats
} // namespace Envoy
