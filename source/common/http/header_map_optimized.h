#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/http/header_map.h"

#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"

namespace Envoy {
namespace Http {

/**
 * Optimized implementation of Http::HeaderMap that uses absl::flat_hash_map for O(1) lookups and a
 * vector for O(1) iteration. This implementation provides performance improvements over the
 * standard implementation.
 */
class HeaderMapOptimized final : public HeaderMap {
public:
  class HeaderEntry final : public Http::HeaderEntry {
  public:
    HeaderEntry(const LowerCaseString& key, const absl::string_view value,
                const bool is_reference) {
      key_.setCopy(key.get());
      if (is_reference) {
        value_.setReference(value);
      } else {
        value_.setCopy(value);
      }
    }

    // HeaderEntry implementation
    const HeaderString& key() const override { return key_; }
    void value(const absl::string_view value) override { value_.setCopy(value); }
    void value(const uint64_t value) override { value_.setInteger(value); }
    void value(const Http::HeaderEntry& header) override {
      value_.setCopy(header.value().getStringView());
    }
    HeaderString& value() override { return value_; }
    const HeaderString& value() const override { return value_; }

    // Helper methods for internal use
    void setValue(const absl::string_view value) { value_.setCopy(value); }
    void setReference(const absl::string_view value) { value_.setReference(value); }
    void setInteger(const uint64_t value) { value_.setInteger(value); }

  private:
    HeaderString key_;
    HeaderString value_;
  };

  explicit HeaderMapOptimized(const uint32_t max_headers_kb = UINT32_MAX,
                              const uint32_t max_headers_count = UINT32_MAX)
      : max_headers_kb_(max_headers_kb), max_headers_count_(max_headers_count) {
    // Reserve space to avoid frequent re-allocations for common cases
    entries_.reserve(16);
    index_map_.reserve(16);
  }

  ~HeaderMapOptimized() override = default;

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

    ConstIterator(const std::vector<std::unique_ptr<HeaderEntry>>* entries, const size_t index)
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

  bool wouldExceedLimits(size_t new_key_size, size_t new_value_size) const;

private:
  // Custom hasher for heterogeneous lookup to avoid string allocations
  struct StringHasher {
    using is_transparent = void;

    size_t operator()(const std::string& str) const { return absl::Hash<std::string>{}(str); }

    size_t operator()(const absl::string_view sv) const {
      return absl::Hash<absl::string_view>{}(sv);
    }
  };

  // Custom equality for heterogeneous lookup
  struct StringEqual {
    using is_transparent = void;

    bool operator()(const std::string& lhs, const std::string& rhs) const { return lhs == rhs; }

    bool operator()(const std::string& lhs, const absl::string_view rhs) const {
      return lhs == rhs;
    }

    bool operator()(const absl::string_view lhs, const std::string& rhs) const {
      return lhs == rhs;
    }
  };

  void updateByteSize(const LowerCaseString& key, absl::string_view value, bool add);

  // Helper method to safely extract string_view from LowerCaseString
  static absl::string_view getKeyView(const LowerCaseString& key) { return key.get(); }

  // Type alias for the optimized hash map with heterogeneous lookup
  using IndexMap = absl::flat_hash_map<std::string, size_t, StringHasher, StringEqual>;

  std::vector<std::unique_ptr<HeaderEntry>> entries_;
  IndexMap index_map_;
  uint32_t max_headers_kb_;
  uint32_t max_headers_count_;
  uint64_t byte_size_{0};
  StatefulHeaderKeyFormatterOptRef formatter_;
};

} // namespace Http
} // namespace Envoy
