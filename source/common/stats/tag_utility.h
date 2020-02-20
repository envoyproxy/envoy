#pragma once

#include "envoy/stats/symbol_table.h"
#include "envoy/stats/tag.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {
namespace TagUtility {

/**
 * Combines a stat name with its tag to create the final stat name to use.
 */
class TagStatNameJoiner {
public:
  /**
   * Combines a prefix, stat name and tags into a single stat name.
   * @param prefix StaName the stat prefix to use.
   * @param name StaName the stat name to use.
   * @param stat_name_tags StatNameTagVector the stat name tags to add to the stat name.
   */
  TagStatNameJoiner(StatName prefix, StatName stat_name, const StatNameTagVector& stat_name_tags,
                    SymbolTable& symbol_table) {
    prefix_storage_ = symbol_table.join({prefix, stat_name});
    name_ = StatName(prefix_storage_.get());
    full_name_storage_ =
        joinNameAndTags(StatName(prefix_storage_.get()), stat_name_tags, symbol_table);
  }

  /**
   * Combines a stat name and tags into a single stat name.
   * @param name StaName the stat name to use.
   * @param stat_name_tags StatNameTagVector the stat name tags to add to the stat name.
   */
  TagStatNameJoiner(StatName stat_name, const StatNameTagVector& stat_name_tags,
                    SymbolTable& symbol_table) {
    name_ = stat_name;
    full_name_storage_ = joinNameAndTags(stat_name, stat_name_tags, symbol_table);
  }

  /**
   * @return StatName the full stat name.
   */
  StatName fullStatName() const { return StatName(full_name_storage_.get()); }

  /**
   * @return StatName the stat name without the tags appended.
   */
  StatName statNameNoTags() const { return name_; }

private:
  StatName name_;
  SymbolTable::StoragePtr prefix_storage_;
  SymbolTable::StoragePtr full_name_storage_;

  SymbolTable::StoragePtr joinNameAndTags(StatName name, const StatNameTagVector& stat_name_tags,
                                          SymbolTable& symbol_table);
};
} // namespace TagUtility
} // namespace Stats
} // namespace Envoy