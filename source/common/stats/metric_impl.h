#pragma once

#include <string>
#include <vector>

#include "envoy/stats/allocator.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/tag.h"

#include "common/common/assert.h"
#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

/**
 * Helper class for implementing Metrics. This does not participate in any
 * inheritance chains, but can be instantiated by classes that do. It just
 * implements the mechanics of representing the name, tag-extracted-name,
 * and all tags as a StatNameList.
 */
class MetricHelper {
public:
  MetricHelper(absl::string_view name, absl::string_view tag_extracted_name,
               const std::vector<Tag>& tags, SymbolTable& symbol_table);
  ~MetricHelper();

  StatName statName() const;
  std::string name(const SymbolTable& symbol_table) const;
  std::vector<Tag> tags(const SymbolTable& symbol_table) const;
  StatName tagExtractedStatName() const;
  void iterateTagStatNames(const Metric::TagStatNameIterFn& fn) const;
  void clear(SymbolTable& symbol_table) { stat_names_.clear(symbol_table); }

private:
  StatNameList stat_names_;
};

/**
 * Partial implementation of the Metric interface on behalf of Counters, Gauges,
 * and Histograms. It leaves symbolTable() unimplemented so that implementations
 * of stats managed by an allocator, specifically Counters and Gauges, can keep
 * a reference to the allocator instead, and derive the symbolTable() from that.
 *
 * We templatize on the base class (Counter, Gauge, or Histogram), rather than
 * using multiple virtual inheritance, as this avoids the overhead of an extra
 * vptr per instance. This is important for stats because there can be many
 * stats in systems with large numbers of clusters and hosts, and a few 8-byte
 * pointers per-stat here and there can add up to significant amounts of memory.
 *
 * Note the delegation of the implementation to a helper class, which is neither
 * templatized nor virtual. This avoids having the compiler elaborate complete
 * copies of the underlying implementation for each base class during template
 * expansion.
 */
template <class BaseClass> class MetricImpl : public BaseClass {
public:
  MetricImpl(absl::string_view name, absl::string_view tag_extracted_name,
             const std::vector<Tag>& tags, SymbolTable& symbol_table)
      : helper_(name, tag_extracted_name, tags, symbol_table) {}

  // Alternate API to take the name as a StatName, which is needed at most call-sites.
  // TODO(jmarantz): refactor impl to either be able to pass string_view at call-sites
  // always, or to make it more efficient to populate a StatNameList with a mixture of
  // StatName and string_view.
  MetricImpl(StatName name, absl::string_view tag_extracted_name, const std::vector<Tag>& tags,
             SymbolTable& symbol_table)
      : MetricImpl(symbol_table.toString(name), tag_extracted_name, tags, symbol_table) {}

  explicit MetricImpl(SymbolTable& symbol_table)
      : MetricImpl("", "", std::vector<Tag>(), symbol_table) {}

  std::vector<Tag> tags() const override { return helper_.tags(constSymbolTable()); }
  StatName statName() const override { return helper_.statName(); }
  StatName tagExtractedStatName() const override { return helper_.tagExtractedStatName(); }
  void iterateTagStatNames(const Metric::TagStatNameIterFn& fn) const override {
    helper_.iterateTagStatNames(fn);
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
  std::string tagExtractedName() const override {
    return constSymbolTable().toString(this->tagExtractedStatName());
  }

protected:
  void clear(SymbolTable& symbol_table) { helper_.clear(symbol_table); }

private:
  MetricHelper helper_;
};

} // namespace Stats
} // namespace Envoy
