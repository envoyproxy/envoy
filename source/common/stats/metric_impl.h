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

  std::string name() const override { return constSymbolTable().toString(statName()); }
  std::string tagExtractedName() const override;
  std::vector<Tag> tags() const override;
  StatName tagExtractedStatName() const override;
  void iterateTagStatNames(const TagStatNameIterFn& fn) const override;
  void iterateTags(const TagIterFn& fn) const override;

  // Metric implementations must each implement Metric::symbolTable(). However,
  // they can inherit the const version of that accessor from MetricImpl.
  const SymbolTable& constSymbolTable() const override {
    // Cast our 'this', which is of type `const MetricImpl*` to a non-const
    // pointer, so we can use it to call the subclass implementation of
    // symbolTable(). That will be returned as a non-const SymbolTable&,
    // which will become const on return.
    //
    // This pattern is used to share a single non-trivial implementation to
    // provide const and non-const variants of a method.
    return const_cast<MetricImpl*>(this)->symbolTable();
  }

protected:
  void clear();

private:
  StatNameList stat_names_;
};

class NullMetricImpl : public MetricImpl {
public:
  explicit NullMetricImpl(SymbolTable& symbol_table)
      : MetricImpl("", std::vector<Tag>(), symbol_table), stat_name_storage_("", symbol_table) {}

  SymbolTable& symbolTable() override { return stat_name_storage_.symbolTable(); }
  bool used() const override { return false; }
  StatName statName() const override { return stat_name_storage_.statName(); }

private:
  StatNameManagedStorage stat_name_storage_;
};

} // namespace Stats
} // namespace Envoy
