#include "source/common/stats/metric_impl.h"

#include "envoy/stats/tag.h"

#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Stats {

MetricHelper::~MetricHelper() {
  // The storage must be cleaned by a subclass of MetricHelper in its
  // destructor, because the symbol-table is owned by the subclass.
  // Simply call MetricHelper::clear() in the subclass dtor.
  ASSERT(!stat_names_.populated());
}

MetricHelper::MetricHelper(StatName name, StatName tag_extracted_name,
                           StatNameTagSpan stat_name_tags, SymbolTable& symbol_table) {
  // Encode all the names and tags into transient storage so we can count the
  // required bytes. 2 is added to account for the name and tag_extracted_name,
  // and we multiply the number of tags by 2 to account for the name and value
  // of each tag.
  const uint32_t num_names = 2 + 2 * stat_name_tags.size();
  absl::FixedArray<StatName> names(num_names);
  names[0] = name;
  names[1] = tag_extracted_name;
  int index = 1;
  for (auto& stat_name_tag : stat_name_tags) {
    names[++index] = stat_name_tag.first;
    names[++index] = stat_name_tag.second;
  }
  symbol_table.populateList(names.begin(), num_names, stat_names_);
}

StatName MetricHelper::statName() const { return stat_names_.at(0); }

StatName MetricHelper::tagExtractedStatName() const { return stat_names_.at(1); }

void MetricHelper::iterateTagStatNames(const Metric::TagStatNameIterFn& fn) const {
  enum { Name, TagExtractedName, TagName, TagValue } state = Name;
  StatName tag_name;

  // StatNameList maintains a linear ordered collection of StatNames, and we
  // are mapping that into a tag-extracted name (the first element), followed
  // by alternating TagName and TagValue. So we use a little state machine
  // as we iterate through the stat_names_.
  stat_names_.iterate([&state, &tag_name, &fn](StatName stat_name) -> bool {
    switch (state) {
    case Name:
      state = TagExtractedName;
      break;
    case TagExtractedName:
      state = TagName;
      break;
    case TagName:
      tag_name = stat_name;
      state = TagValue;
      break;
    case TagValue:
      state = TagName;
      if (!fn(tag_name, stat_name)) {
        return false; // early exit.
      }
      break;
    }
    return true;
  });
  ASSERT(state != TagValue);
}

TagVector MetricHelper::tags(const SymbolTable& symbol_table) const {
  TagVector tags;
  iterateTagStatNames([&tags, &symbol_table](StatName name, StatName value) -> bool {
    tags.emplace_back(Tag{symbol_table.toString(name), symbol_table.toString(value)});
    return true;
  });
  return tags;
}

} // namespace Stats
} // namespace Envoy
