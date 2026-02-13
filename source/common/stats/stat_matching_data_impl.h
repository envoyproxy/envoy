#pragma once

#include "envoy/stats/stats.h"

#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Stats {

template <class StatType> class StatMatchingDataImpl : public StatMatchingData {
public:
  StatMatchingDataImpl(const StatType& metric) : metric_(metric) {}

  static std::string name() { return "stat_matching_data_impl"; }

  std::string fullName() const override { return metric_.name(); }

  absl::optional<std::reference_wrapper<const SymbolTable>> symbolTable() const override {
    if constexpr (requires(const StatType& m) { m.constSymbolTable(); }) {
      return metric_.constSymbolTable();
    } else {
      return absl::nullopt;
    }
  }

  std::string tagValue(const StatName& name) const override {
    if constexpr (requires(const StatType& m) {
                    m.iterateTagStatNames(std::declval<Metric::TagStatNameIterFn>());
                  }) {
      std::string value;
      metric_.iterateTagStatNames([&](StatName tag_name, StatName tag_value) -> bool {
        if (tag_name == name) {
          value = metric_.constSymbolTable().toString(tag_value);
          return false;
        }
        return true;
      });
      return value;
    } else {
      // Fallback for primitive stats which don't have symbol tables.
      // We have to convert name to string for comparison.
      // We don't have a SymbolTable here, so we might need one to convert StatName back to string.
      // This is a limitation for primitive stats when using StatName-based tag matching.
      return "";
    }
  }

private:
  const StatType& metric_;
};

} // namespace Stats
} // namespace Envoy
