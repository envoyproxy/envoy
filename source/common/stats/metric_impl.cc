#include "common/stats/metric_impl.h"

#include "envoy/stats/tag.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

MetricImpl::MetricImpl(absl::string_view tag_extracted_name, const std::vector<Tag>& tags,
                       SymbolTable& symbol_table) {
  ASSERT(tags.size() < 256);

  // Encode all the names and tags into transient storage so we can count the
  // required bytes.
  SymbolEncoding tag_extracted_stat_name = symbol_table.encode(tag_extracted_name);
  size_t total_size = 1 /* for tags.size() */ + tag_extracted_stat_name.bytesRequired();
  std::vector<std::pair<SymbolEncoding, SymbolEncoding>> sym_vecs;
  sym_vecs.resize(tags.size());
  int index = 0;
  for (auto tag : tags) {
    auto& vecs = sym_vecs[index++];
    SymbolEncoding x = symbol_table.encode(tag.name_);
    vecs.first.swap(x);
    SymbolEncoding y = symbol_table.encode(tag.value_);
    vecs.second.swap(y);
    total_size += vecs.first.bytesRequired() + vecs.second.bytesRequired();
  }
  storage_ = std::make_unique<uint8_t[]>(total_size);
  uint8_t* p = &storage_[0];
  *p++ = tags.size();
  p += tag_extracted_stat_name.moveToStorage(p);
  for (auto& sym_vec_pair : sym_vecs) {
    p += sym_vec_pair.first.moveToStorage(p);
    p += sym_vec_pair.second.moveToStorage(p);
  }
  ASSERT(p == &storage_[0] + total_size);
}

void MetricImpl::clear() {
  SymbolTable& symbol_table = symbolTable();
  uint8_t* p = &storage_[0];
  uint32_t num_tags = *p++;
  p += tagExtractedStatName().numBytesIncludingLength();
  symbol_table.free(tagExtractedStatName());
  for (size_t i = 0; i < num_tags; ++i) {
    Tag tag;
    StatName name(p);
    p += name.numBytesIncludingLength();
    symbol_table.free(name);
    StatName value(p);
    p += value.numBytesIncludingLength();
    symbol_table.free(value);
  }
  storage_.reset();
}

MetricImpl::~MetricImpl() {
  // The storage must be cleaned by a subclass of MetricImpl in its
  // destructor, because the symbol-table is owned by the subclass.
  // Simply call MetricImpl::clear() in the subclass dtor.
  ASSERT(storage_ == nullptr);
}

std::string MetricImpl::tagExtractedName() const {
  return tagExtractedStatName().toString(symbolTable());
}

StatName MetricImpl::tagExtractedStatName() const { return StatName(&storage_[1]); }

std::vector<Tag> MetricImpl::tags() const {
  uint8_t* p = &storage_[0];
  uint32_t num_tags = *p++;
  p += tagExtractedStatName().numBytesIncludingLength();

  std::vector<Tag> tags;
  tags.reserve(num_tags);
  const SymbolTable& symbol_table = symbolTable();

  for (size_t i = 0; i < num_tags; ++i) {
    Tag tag;
    StatName name(p);
    tag.name_ = name.toString(symbol_table);
    p += name.numBytesIncludingLength();
    StatName value(p);
    tag.value_ = value.toString(symbol_table);
    p += value.numBytesIncludingLength();
    tags.emplace_back(tag);
  }
  return tags;
}

} // namespace Stats
} // namespace Envoy
