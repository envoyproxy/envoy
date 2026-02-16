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

  const SymbolTable& symbolTable() const override { return metric_.constSymbolTable(); }

  std::string tagValue(const StatName& name) const override {
    std::string value;
    metric_.iterateTagStatNames([&](StatName tag_name, StatName tag_value) -> bool {
      if (tag_name == name) {
        value = symbolTable().toString(tag_value);
        return false;
      }
      return true;
    });
    return value;
  }

private:
  const StatType& metric_;
};

} // namespace Stats
} // namespace Envoy
