#include "common/stats/metric_impl.h"

#include "envoy/stats/tag.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

MetricHelper::~MetricHelper() {
  // The storage must be cleaned by a subclass of MetricHelper in its
  // destructor, because the symbol-table is owned by the subclass.
  // Simply call MetricHelper::clear() in the subclass dtor.
  ASSERT(!stat_names_.populated());
}

MetricHelper::MetricHelper(absl::string_view tag_extracted_name, const std::vector<Tag>& tags,
                           SymbolTable& symbol_table) {
  // Encode all the names and tags into transient storage so we can count the
  // required bytes. 1 is added to account for the tag_extracted_name, and we
  // multiply the number of tags by 2 to account for the name and value of each
  // tag.
  const uint32_t num_names = 1 + 2 * tags.size();
  STACK_ARRAY(names, absl::string_view, num_names);
  names[0] = tag_extracted_name;
  int index = 0;
  for (auto& tag : tags) {
    names[++index] = tag.name_;
    names[++index] = tag.value_;
  }
  symbol_table.populateList(names.begin(), num_names, stat_names_);
}

std::string MetricHelper::tagExtractedName(const SymbolTable& symbol_table) const {
  return symbol_table.toString(tagExtractedStatName());
}

StatName MetricHelper::tagExtractedStatName() const {
  StatName stat_name;
  stat_names_.iterate([&stat_name](StatName s) -> bool {
    stat_name = s;
    return false; // Returning 'false' stops the iteration.
  });
  return stat_name;
}

void MetricHelper::iterateTagStatNames(const Metric::TagStatNameIterFn& fn) const {
  enum { TagExtractedName, TagName, TagValue } state = TagExtractedName;
  StatName tag_name;

  // StatNameList maintains a linear ordered collection of StatNames, and we
  // are mapping that into a tag-extracted name (the first element), followed
  // by alternating TagName and TagValue. So we use a little state machine
  // as we iterate through the stat_names_.
  stat_names_.iterate([&state, &tag_name, &fn](StatName stat_name) -> bool {
    switch (state) {
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

void MetricHelper::iterateTags(const SymbolTable& symbol_table, const Metric::TagIterFn& fn) const {
  iterateTagStatNames([&fn, &symbol_table](StatName name, StatName value) -> bool {
    return fn(Tag{symbol_table.toString(name), symbol_table.toString(value)});
  });
}

std::vector<Tag> MetricHelper::tags(const SymbolTable& symbol_table) const {
  std::vector<Tag> tags;
  iterateTags(symbol_table, [&tags](const Tag& tag) -> bool {
    tags.emplace_back(tag);
    return true;
  });
  return tags;
}

} // namespace Stats
} // namespace Envoy
