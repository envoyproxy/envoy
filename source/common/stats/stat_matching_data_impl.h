#pragma once

#include <memory>

#include "envoy/stats/stats.h"

#include "source/common/stats/symbol_table.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Stats {

// A struct containing the pre-computed string values of a stat's name and tags.
// This allows tag matching to avoid looking up StatName in SymbolTable
// (which requires a lock).
struct NameAndTags {
  NameAndTags(std::string name, absl::flat_hash_map<std::string, uint32_t> tag_map)
      : name_(std::move(name)), tag_map_(std::move(tag_map)) {}

  std::string name_;
  // Map from tag name (string) to the index of tag in a TagVector.
  absl::flat_hash_map<std::string, uint32_t> tag_map_;
};

template <class StatType> class StatMatchingDataImpl : public StatMatchingData {
public:
  StatMatchingDataImpl(const StatType& metric, const NameAndTags& name_and_tags)
      : metric_(metric), name_and_tags_(name_and_tags) {}

  static std::string name() { return "stat_matching_data_impl"; }

  std::string fullName() const override { return name_and_tags_.name_; }

  std::string tagValue(absl::string_view name) const override {
    auto it = name_and_tags_.tag_map_.find(name);
    if (it != name_and_tags_.tag_map_.end()) {
      // Tags vector order should map to indices in NameAndTags map.
      return metric_.tags()[it->second].value_;
    }
    return {};
  }

private:
  const StatType& metric_;
  const NameAndTags& name_and_tags_;
};

} // namespace Stats
} // namespace Envoy
