#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/http/header_map.h"

#include "source/common/common/non_copyable.h"
#include "source/common/http/headers.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Http {

/**
 * Optimized implementation of Http::HeaderMap that uses a more efficient data structure
 * for header storage and lookup.
 */
class HeaderMapOptimized : public Http::HeaderMap {
public:
  class HeaderEntry : public Http::HeaderEntry {
  public:
    HeaderEntry(const Http::LowerCaseString& key, absl::string_view value, bool is_reference)
        : key_(key.get()) {
      if (is_reference) {
        value_.setReference(value);
      } else {
        value_.setCopy(value);
      }
    }

    // HeaderEntry implementation
    const HeaderString& key() const override { return key_; }
    void value(absl::string_view value) override { value_.setCopy(value); }
    void value(uint64_t value) override { value_.setInteger(value); }
    void value(const Http::HeaderEntry& header) override {
      value_.setCopy(header.value().getStringView());
    }
    HeaderString& value() override { return value_; }
    const HeaderString& value() const override { return value_; }

    // Helper methods
    void setValue(absl::string_view value) { value_.setCopy(value); }
    void setReference(absl::string_view value) { value_.setReference(value); }
    void setInteger(uint64_t value) { value_.setInteger(value); }
    absl::string_view getStringView() const { return value_.getStringView(); }

  private:
    HeaderString key_;
    HeaderString value_;
  };

  HeaderMapOptimized(const uint32_t max_headers_kb = UINT32_MAX,
                     const uint32_t max_headers_count = UINT32_MAX)
      : max_headers_kb_(max_headers_kb), max_headers_count_(max_headers_count) {}
  virtual ~HeaderMapOptimized() = default;

  // HeaderMap implementation
  void addReference(const LowerCaseString& key, absl::string_view value) override;
  void addReferenceKey(const LowerCaseString& key, uint64_t value) override;
  void addReferenceKey(const LowerCaseString& key, absl::string_view value) override;
  void addCopy(const LowerCaseString& key, uint64_t value) override;
  void addCopy(const LowerCaseString& key, absl::string_view value) override;
  void appendCopy(const LowerCaseString& key, absl::string_view value) override;
  void setReference(const LowerCaseString& key, absl::string_view value) override;
  void setReferenceKey(const LowerCaseString& key, absl::string_view value) override;
  void setCopy(const LowerCaseString& key, absl::string_view value) override;
  uint64_t byteSize() const override { return byte_size_; }
  uint32_t maxHeadersKb() const override { return max_headers_kb_; }
  uint32_t maxHeadersCount() const override { return max_headers_count_; }
  GetResult get(const LowerCaseString& key) const override;
  void iterate(ConstIterateCb cb) const override;
  void iterateReverse(ConstIterateCb cb) const override;
  void clear() override;
  size_t remove(const LowerCaseString& key) override;
  size_t removeIf(const HeaderMatchPredicate& predicate) override;
  size_t removePrefix(const LowerCaseString& prefix) override;
  size_t size() const override { return entries_.size(); }
  bool empty() const override { return entries_.empty(); }
  void dumpState(std::ostream& os, int indent_level = 0) const override;
  StatefulHeaderKeyFormatterOptConstRef formatter() const override {
    return StatefulHeaderKeyFormatterOptConstRef(formatter_);
  }
  StatefulHeaderKeyFormatterOptRef formatter() override { return formatter_; }
  bool operator==(const HeaderMap& rhs) const override;
  bool operator!=(const HeaderMap& rhs) const override { return !(*this == rhs); }
  void addViaMove(HeaderString&& key, HeaderString&& value) override;

  // Iterator interface
  class ConstIterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = HeaderEntry;
    using difference_type = std::ptrdiff_t;
    using pointer = const HeaderEntry*;
    using reference = const HeaderEntry&;

    ConstIterator(const std::vector<std::unique_ptr<HeaderEntry>>* entries, size_t index)
        : entries_(entries), index_(index) {}

    reference operator*() const { return *(*entries_)[index_]; }
    pointer operator->() const { return (*entries_)[index_].get(); }
    ConstIterator& operator++() {
      ++index_;
      return *this;
    }
    bool operator==(const ConstIterator& other) const {
      return entries_ == other.entries_ && index_ == other.index_;
    }
    bool operator!=(const ConstIterator& other) const { return !(*this == other); }

  private:
    const std::vector<std::unique_ptr<HeaderEntry>>* entries_;
    size_t index_;
  };

  ConstIterator begin() const { return ConstIterator(&entries_, 0); }
  ConstIterator end() const { return ConstIterator(&entries_, entries_.size()); }

private:
  void updateByteSize(const LowerCaseString& key, absl::string_view value, bool add);
  bool exceedsLimits() const;

  /**
   * Check if adding a new header with the given key and value sizes would exceed the limits.
   * @param new_key_size size of the new key to be added
   * @param new_value_size size of the new value to be added
   * @return true if adding would exceed limits, false otherwise
   */
  bool wouldExceedLimits(size_t new_key_size, size_t new_value_size) const;

  std::vector<std::unique_ptr<HeaderEntry>> entries_;
  absl::flat_hash_map<std::string, size_t> index_map_;
  uint32_t max_headers_kb_;
  uint32_t max_headers_count_;
  uint64_t byte_size_{0};
  StatefulHeaderKeyFormatterOptRef formatter_;
};

} // namespace Http
} // namespace Envoy
