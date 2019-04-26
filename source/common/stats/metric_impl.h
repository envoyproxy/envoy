#pragma once

#include <string>
#include <vector>

#include "envoy/stats/stat_data_allocator.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/tag.h"

#include "common/common/assert.h"
#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

/**
 * Implementation of the Metric interface. Virtual inheritance is used because the interfaces that
 * will inherit from Metric will have other base classes that will also inherit from Metric.
 *
 * MetricImpl is not meant to be instantiated as-is. For performance reasons we keep name() virtual
 * and expect child classes to implement it.
 */
class MetricImpl : public virtual Metric {
public:
  MetricImpl(absl::string_view tag_extracted_name, const std::vector<Tag>& tags,
             SymbolTable& symbol_table);
  ~MetricImpl();

  std::string name() const override { return symbolTable().toString(statName()); }
  std::string tagExtractedName() const override;
  std::vector<Tag> tags() const override;
  StatName tagExtractedStatName() const override;

protected:
  /**
   * Flags used by all stats types to figure out whether they have been used.
   */
  struct Flags {
    static const uint8_t Used = 0x1;
  };

  void clear();

private:
  StatNameList stat_names_;
};

class NullMetricImpl : public MetricImpl {
public:
  explicit NullMetricImpl(SymbolTable& symbol_table)
      : MetricImpl("", std::vector<Tag>(), symbol_table), stat_name_storage_("", symbol_table) {}

  const SymbolTable& symbolTable() const override { return stat_name_storage_.symbolTable(); }
  SymbolTable& symbolTable() override { return stat_name_storage_.symbolTable(); }
  bool used() const override { return false; }
  StatName statName() const override { return stat_name_storage_.statName(); }

private:
  StatNameManagedStorage stat_name_storage_;
};

} // namespace Stats
} // namespace Envoy
