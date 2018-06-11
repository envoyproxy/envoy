#pragma once

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/http/header_map.h"

#include "common/common/non_copyable.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Http {

/**
 * These are definitions of all of the inline header access functions described inside header_map.h
 */
#define DEFINE_INLINE_HEADER_FUNCS(name)                                                           \
public:                                                                                            \
  const HeaderEntry* name() const override { return inline_headers_.name##_; }                     \
  HeaderEntry* name() override { return inline_headers_.name##_; }                                 \
  HeaderEntry& insert##name() override {                                                           \
    return maybeCreateInline(&inline_headers_.name##_, Headers::get().name);                       \
  }                                                                                                \
  void remove##name() override { removeInline(&inline_headers_.name##_); }

#define DEFINE_INLINE_HEADER_STRUCT(name) HeaderEntryImpl* name##_;

/**
 * Implementation of Http::HeaderMap. This is heavily optimized for performance. Roughly, when
 * headers are added to the map, we do a hash lookup to see if it's one of the O(1) headers.
 * If it is, we store a reference to it that can be accessed later directly. Most high performance
 * paths use O(1) direct access. In general, we try to copy as little as possible and allocate as
 * little as possible in any of the paths.
 */
class HeaderMapImpl : public HeaderMap {
public:
  /**
   * Appends data to header. If header already has a value, the string ',' is added between the
   * existing value and data.
   * @param header the header to append to.
   * @param data to append to the header.
   */
  static void appendToHeader(HeaderString& header, const std::string& data);

  HeaderMapImpl();
  HeaderMapImpl(const std::initializer_list<std::pair<LowerCaseString, std::string>>& values);
  HeaderMapImpl(const HeaderMap& rhs);

  /**
   * Add a header via full move. This is the expected high performance paths for codecs populating
   * a map when receiving.
   */
  void addViaMove(HeaderString&& key, HeaderString&& value);

  /**
   * For testing. Equality is based on equality of the backing list. This is an exact match
   * comparison (order matters).
   */
  bool operator==(const HeaderMapImpl& rhs) const;

  // Http::HeaderMap
  void addReference(const LowerCaseString& key, const std::string& value) override;
  void addReferenceKey(const LowerCaseString& key, uint64_t value) override;
  void addReferenceKey(const LowerCaseString& key, const std::string& value) override;
  void addCopy(const LowerCaseString& key, uint64_t value) override;
  void addCopy(const LowerCaseString& key, const std::string& value) override;
  void setReference(const LowerCaseString& key, const std::string& value) override;
  void setReferenceKey(const LowerCaseString& key, const std::string& value) override;
  uint64_t byteSize() const override;
  const HeaderEntry* get(const LowerCaseString& key) const override;
  HeaderEntry* get(const LowerCaseString& key) override;
  void iterate(ConstIterateCb cb, void* context) const override;
  void iterateReverse(ConstIterateCb cb, void* context) const override;
  Lookup lookup(const LowerCaseString& key, const HeaderEntry** entry) const override;
  void remove(const LowerCaseString& key) override;
  void removePrefix(const LowerCaseString& key) override;
  size_t size() const override { return headers_.size(); }

protected:
  struct HeaderEntryImpl : public HeaderEntry, NonCopyable {
    HeaderEntryImpl(const LowerCaseString& key);
    HeaderEntryImpl(const LowerCaseString& key, HeaderString&& value);
    HeaderEntryImpl(HeaderString&& key, HeaderString&& value);

    // HeaderEntry
    const HeaderString& key() const override { return key_; }
    void value(const char* value, uint32_t size) override;
    void value(const std::string& value) override;
    void value(uint64_t value) override;
    void value(const HeaderEntry& header) override;
    const HeaderString& value() const override { return value_; }
    HeaderString& value() override { return value_; }

    HeaderString key_;
    HeaderString value_;
    std::list<HeaderEntryImpl>::iterator entry_;
  };

  struct StaticLookupResponse {
    HeaderEntryImpl** entry_;
    const LowerCaseString* key_;
  };

  struct StaticLookupEntry {
    typedef StaticLookupResponse (*EntryCb)(HeaderMapImpl&);

    EntryCb cb_{};
    std::array<std::unique_ptr<StaticLookupEntry>, 256> entries_;
  };

  /**
   * This is the static lookup table that is used to determine whether a header is one of the O(1)
   * headers. This uses a trie for lookup time at most equal to the size of the incoming string.
   */
  struct StaticLookupTable {
    StaticLookupTable();
    void add(const char* key, StaticLookupEntry::EntryCb cb);
    StaticLookupEntry::EntryCb find(const char* key) const;

    StaticLookupEntry root_;
  };

  struct AllInlineHeaders {
    ALL_INLINE_HEADERS(DEFINE_INLINE_HEADER_STRUCT)
  };

  /**
   * List of HeaderEntryImpl that keeps the pseudo headers (key starting with ':') in the front
   * of the list (as required by nghttp2) and otherwise maintains insertion order.
   */
  class HeaderList {
  public:
    HeaderList() : pseudo_headers_end_(headers_.end()) {}

    template <class Key> bool isPseudoHeader(const Key& key) { return key.c_str()[0] == ':'; }

    template <class Key, class... Value>
    std::list<HeaderEntryImpl>::iterator insert(Key&& key, Value&&... value) {
      const bool is_pseudo_header = isPseudoHeader(key);
      std::list<HeaderEntryImpl>::iterator i =
          headers_.emplace(is_pseudo_header ? pseudo_headers_end_ : headers_.end(),
                           std::forward<Key>(key), std::forward<Value>(value)...);
      if (!is_pseudo_header && pseudo_headers_end_ == headers_.end()) {
        pseudo_headers_end_ = i;
      }
      return i;
    }

    std::list<HeaderEntryImpl>::iterator erase(std::list<HeaderEntryImpl>::iterator i) {
      if (pseudo_headers_end_ == i) {
        pseudo_headers_end_++;
      }
      return headers_.erase(i);
    }

    template <class UnaryPredicate> void remove_if(UnaryPredicate p) {
      headers_.remove_if([&](const HeaderEntryImpl& entry) {
        const bool to_remove = p(entry);
        if (to_remove) {
          if (pseudo_headers_end_ == entry.entry_) {
            pseudo_headers_end_++;
          }
        }
        return to_remove;
      });
    }

    std::list<HeaderEntryImpl>::iterator begin() { return headers_.begin(); }
    std::list<HeaderEntryImpl>::iterator end() { return headers_.end(); }
    std::list<HeaderEntryImpl>::const_iterator begin() const { return headers_.begin(); }
    std::list<HeaderEntryImpl>::const_iterator end() const { return headers_.end(); }
    std::list<HeaderEntryImpl>::const_reverse_iterator rbegin() const { return headers_.rbegin(); }
    std::list<HeaderEntryImpl>::const_reverse_iterator rend() const { return headers_.rend(); }
    size_t size() const { return headers_.size(); }

  private:
    std::list<HeaderEntryImpl> headers_;
    std::list<HeaderEntryImpl>::iterator pseudo_headers_end_;
  };

  void insertByKey(HeaderString&& key, HeaderString&& value);
  HeaderEntryImpl& maybeCreateInline(HeaderEntryImpl** entry, const LowerCaseString& key);
  HeaderEntryImpl& maybeCreateInline(HeaderEntryImpl** entry, const LowerCaseString& key,
                                     HeaderString&& value);
  HeaderEntryImpl* getExistingInline(const char* key);

  void removeInline(HeaderEntryImpl** entry);

  AllInlineHeaders inline_headers_;
  HeaderList headers_;

  ALL_INLINE_HEADERS(DEFINE_INLINE_HEADER_FUNCS)
};

typedef std::unique_ptr<HeaderMapImpl> HeaderMapImplPtr;

} // Http
} // namespace Envoy
