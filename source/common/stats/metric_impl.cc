#include "common/stats/metric_impl.h"

#include "envoy/stats/tag.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

MetricImpl::~MetricImpl() {
  // The storage must be cleaned by a subclass of MetricImpl in its
  // destructor, because the symbol-table is owned by the subclass.
  // Simply call MetricImpl::clear() in the subclass dtor.
  ASSERT(!stat_names_.populated());
}

MetricImpl::MetricImpl(absl::string_view tag_extracted_name, const std::vector<Tag>& tags,
                       SymbolTable& symbol_table) {
  // Encode all the names and tags into transient storage so we can count the
  // required bytes.
  uint32_t num_names = 1 /* tag_extracted_name */ + 2 * tags.size();
  STACK_ARRAY(names, absl::string_view, num_names);
  names[0] = tag_extracted_name;
  int index = 0;
  for (auto& tag : tags) {
    names[++index] = tag.name_;
    names[++index] = tag.value_;
  }
  symbol_table.populateList(names.begin(), num_names, stat_names_);
}

void MetricImpl::clear() { stat_names_.clear(symbolTable()); }

std::string MetricImpl::tagExtractedName() const {
  return symbolTable().toString(tagExtractedStatName());
}

StatName MetricImpl::tagExtractedStatName() const {
  StatName stat_name;
  stat_names_.iterate([&stat_name](StatName s) -> bool {
    stat_name = s;
    return false;
  });
  return stat_name;
}

std::vector<Tag> MetricImpl::tags() const {
  std::vector<Tag> tags;
  enum { TagExtractedName, Name, Value } state = TagExtractedName;
  Tag tag;
  const SymbolTable& symbol_table = symbolTable();
  stat_names_.iterate([&tags, &state, &tag, &symbol_table](StatName stat_name) -> bool {
    switch (state) {
    case TagExtractedName:
      state = Name;
      break;
    case Name:
      tag.name_ = symbol_table.toString(stat_name);
      state = Value;
      break;
    case Value:
      tag.value_ = symbol_table.toString(stat_name);
      tags.emplace_back(tag);
      state = Name;
    }
    return true;
  });
  ASSERT(state != Value);
  return tags;
}

} // namespace Stats
} // namespace Envoy
