#pragma once

#include <memory>

#include "envoy/stats/stats.h"

#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Stats {
template <class StatType> class StatMatchingDataImpl : public StatMatchingData {
public:
  StatMatchingDataImpl(const StatType& metric, const SymbolTable& symbol_table)
      : metric_(metric), symbol_table_(symbol_table) {}

  static std::string name() { return "stat_matching_data_impl"; }

  std::string fullName() const override { return metric_.name(); }

  const SymbolTable& symbolTable() const override { return symbol_table_; }

  std::string tagValue(absl::string_view name) const override {
    if (tags_map_ == nullptr) {
      tags_map_ = std::make_unique<absl::flat_hash_map<std::string, StatName>>();
      metric_.iterateTagStatNames([this](StatName tag_name, StatName tag_value) -> bool {
        tags_map_->emplace(symbolTable().toString(tag_name), tag_value);
        return true;
      });
    }

    auto it = tags_map_->find(name);
    if (it != tags_map_->end()) {
      return symbolTable().toString(it->second);
    }
    return {};
  }

private:
  const StatType& metric_;
  const SymbolTable& symbol_table_;
  // A cache of tag names to tag values. This is mutable so that it can be lazily initialized
  // in tagValue().
  mutable std::unique_ptr<absl::flat_hash_map<std::string, StatName>> tags_map_;
};

} // namespace Stats
} // namespace Envoy
