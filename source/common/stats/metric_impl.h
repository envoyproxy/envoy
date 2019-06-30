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
 * Helper class for implementing Metrics. This does not participate in any,
 * inheritance chains, but can be instantiated by classes that do.
 */
class MetricHelper {
public:
  MetricHelper(absl::string_view tag_extracted_name, const std::vector<Tag>& tags,
               SymbolTable& symbol_table);
  ~MetricHelper();

  std::string tagExtractedName(const SymbolTable& symbol_table) const;
  std::vector<Tag> tags(const SymbolTable& symbol_table) const;
  StatName tagExtractedStatName() const;
  void iterateTagStatNames(const Metric::TagStatNameIterFn& fn) const;
  void iterateTags(const SymbolTable& symbol_table, const Metric::TagIterFn& fn) const;
  void clear(SymbolTable& symbol_table) { stat_names_.clear(symbol_table); }

private:
  StatNameList stat_names_;
};

/**
 * Implementation of the Metric interface. Virtual inheritance is used because the interfaces that
 * will inherit from Metric will have other base classes that will also inherit from Metric.
 *
 * MetricImpl is not meant to be instantiated as-is. For performance reasons we keep name() virtual
 * and expect child classes to implement it.
 */
template <class BaseClass> class MetricImpl : public BaseClass {
public:
  MetricImpl(absl::string_view tag_extracted_name, const std::vector<Tag>& tags,
             SymbolTable& symbol_table)
      : helper_(tag_extracted_name, tags, symbol_table) {}

  explicit MetricImpl(SymbolTable& symbol_table) : helper_("", std::vector<Tag>(), symbol_table) {}

  std::string tagExtractedName() const override {
    return helper_.tagExtractedName(constSymbolTable());
  }
  std::vector<Tag> tags() const override { return helper_.tags(constSymbolTable()); }
  StatName tagExtractedStatName() const override { return helper_.tagExtractedStatName(); }
  void iterateTagStatNames(const Metric::TagStatNameIterFn& fn) const override {
    helper_.iterateTagStatNames(fn);
  }
  void iterateTags(const Metric::TagIterFn& fn) const override {
    helper_.iterateTags(constSymbolTable(), fn);
  }

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
  std::string name() const override { return constSymbolTable().toString(this->statName()); }

protected:
  void clear(SymbolTable& symbol_table) { helper_.clear(symbol_table); }

private:
  MetricHelper helper_;
};

} // namespace Stats
} // namespace Envoy
