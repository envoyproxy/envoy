#include "common/http/header_map_impl.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/http/header_map.h"

#include "common/common/assert.h"
#include "common/common/dump_state_utils.h"
#include "common/common/empty_string.h"
#include "common/runtime/runtime_features.h"
#include "common/singleton/const_singleton.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Http {

namespace {
// This includes the NULL (StringUtil::itoa technically only needs 21).
constexpr size_t MaxIntegerLength{32};

void validateCapacity(uint64_t new_capacity) {
  // If the resizing will cause buffer overflow due to hitting uint32_t::max, an OOM is likely
  // imminent. Fast-fail rather than allow a buffer overflow attack (issue #1421)
  RELEASE_ASSERT(new_capacity <= std::numeric_limits<uint32_t>::max(),
                 "Trying to allocate overly large headers.");
}

absl::string_view getStrView(const VariantHeader& buffer) {
  return absl::get<absl::string_view>(buffer);
}

InlineHeaderVector& getInVec(VariantHeader& buffer) {
  return absl::get<InlineHeaderVector>(buffer);
}

const InlineHeaderVector& getInVec(const VariantHeader& buffer) {
  return absl::get<InlineHeaderVector>(buffer);
}
} // namespace

// Initialize as a Type::Inline
HeaderString::HeaderString() : buffer_(InlineHeaderVector()) {
  ASSERT((getInVec(buffer_).capacity()) >= MaxIntegerLength);
  ASSERT(valid());
}

// Initialize as a Type::Reference
HeaderString::HeaderString(const LowerCaseString& ref_value)
    : buffer_(absl::string_view(ref_value.get().c_str(), ref_value.get().size())) {
  ASSERT(valid());
}

// Initialize as a Type::Reference
HeaderString::HeaderString(absl::string_view ref_value) : buffer_(ref_value) { ASSERT(valid()); }

HeaderString::HeaderString(HeaderString&& move_value) noexcept
    : buffer_(std::move(move_value.buffer_)) {
  move_value.clear();
  ASSERT(valid());
}

bool HeaderString::valid() const { return validHeaderString(getStringView()); }

void HeaderString::append(const char* data, uint32_t data_size) {
  // Make sure the requested memory allocation is below uint32_t::max
  const uint64_t new_capacity = static_cast<uint64_t>(data_size) + size();
  validateCapacity(new_capacity);
  ASSERT(validHeaderString(absl::string_view(data, data_size)));

  switch (type()) {
  case Type::Reference: {
    // Rather than be too clever and optimize this uncommon case, we switch to
    // Inline mode and copy.
    const absl::string_view prev = getStrView(buffer_);
    buffer_ = InlineHeaderVector();
    // Assigning new_capacity to avoid resizing when appending the new data
    getInVec(buffer_).reserve(new_capacity);
    getInVec(buffer_).assign(prev.begin(), prev.end());
    break;
  }
  case Type::Inline: {
    getInVec(buffer_).reserve(new_capacity);
    break;
  }
  }
  getInVec(buffer_).insert(getInVec(buffer_).end(), data, data + data_size);
}

void HeaderString::rtrim() {
  ASSERT(type() == Type::Inline);
  absl::string_view original = getStringView();
  absl::string_view rtrimmed = StringUtil::rtrim(original);
  if (original.size() != rtrimmed.size()) {
    getInVec(buffer_).resize(rtrimmed.size());
  }
}

absl::string_view HeaderString::getStringView() const {
  if (type() == Type::Reference) {
    return getStrView(buffer_);
  }
  ASSERT(type() == Type::Inline);
  return {getInVec(buffer_).data(), getInVec(buffer_).size()};
}

void HeaderString::clear() {
  if (type() == Type::Inline) {
    getInVec(buffer_).clear();
  }
}

void HeaderString::setCopy(const char* data, uint32_t size) {
  ASSERT(validHeaderString(absl::string_view(data, size)));

  if (!absl::holds_alternative<InlineHeaderVector>(buffer_)) {
    // Switching from Type::Reference to Type::Inline
    buffer_ = InlineHeaderVector();
  }

  getInVec(buffer_).reserve(size);
  getInVec(buffer_).assign(data, data + size);
  ASSERT(valid());
}

void HeaderString::setCopy(absl::string_view view) {
  this->setCopy(view.data(), static_cast<uint32_t>(view.size()));
}

void HeaderString::setInteger(uint64_t value) {
  // Initialize the size to the max length, copy the actual data, and then
  // reduce the size (but not the capacity) as needed
  // Note: instead of using the inner_buffer, attempted the following:
  // resize buffer_ to MaxIntegerLength, apply StringUtil::itoa to the buffer_.data(), and then
  // resize buffer_ to int_length (the number of digits in value).
  // However it was slower than the following approach.
  char inner_buffer[MaxIntegerLength];
  const uint32_t int_length = StringUtil::itoa(inner_buffer, MaxIntegerLength, value);

  if (type() == Type::Reference) {
    // Switching from Type::Reference to Type::Inline
    buffer_ = InlineHeaderVector();
  }
  ASSERT((getInVec(buffer_).capacity()) > MaxIntegerLength);
  getInVec(buffer_).assign(inner_buffer, inner_buffer + int_length);
}

void HeaderString::setReference(absl::string_view ref_value) {
  buffer_ = ref_value;
  ASSERT(valid());
}

uint32_t HeaderString::size() const {
  if (type() == Type::Reference) {
    return getStrView(buffer_).size();
  }
  ASSERT(type() == Type::Inline);
  return getInVec(buffer_).size();
}

HeaderString::Type HeaderString::type() const {
  // buffer_.index() is correlated with the order of Reference and Inline in the
  // enum.
  ASSERT(buffer_.index() == 0 || buffer_.index() == 1);
  ASSERT((buffer_.index() == 0 && absl::holds_alternative<absl::string_view>(buffer_)) ||
         (buffer_.index() != 0));
  ASSERT((buffer_.index() == 1 && absl::holds_alternative<InlineHeaderVector>(buffer_)) ||
         (buffer_.index() != 1));
  return Type(buffer_.index());
}

// Specialization needed for HeaderMapImpl::HeaderList::insert() when key is LowerCaseString.
// A fully specialized template must be defined once in the program, hence this may not be in
// a header file.
template <> bool HeaderMapImpl::HeaderList::isPseudoHeader(const LowerCaseString& key) {
  return key.get().c_str()[0] == ':';
}

bool HeaderMapImpl::HeaderList::maybeMakeMap() {
  if (lazy_map_.empty()) {
    if (headers_.size() < lazy_map_min_size_) {
      return false;
    }
    // Add all entries from the list into the map.
    for (auto node = headers_.begin(); node != headers_.end(); ++node) {
      HeaderNodeVector& v = lazy_map_[node->key().getStringView()];
      v.push_back(node);
    }
  }
  return true;
}

size_t HeaderMapImpl::HeaderList::remove(absl::string_view key) {
  size_t removed_bytes = 0;
  if (maybeMakeMap()) {
    auto iter = lazy_map_.find(key);
    if (iter != lazy_map_.end()) {
      // Erase from the map, and all same key entries from the list.
      HeaderNodeVector header_nodes = std::move(iter->second);
      lazy_map_.erase(iter);
      for (const HeaderNode& node : header_nodes) {
        ASSERT(node->key() == key);
        removed_bytes += node->key().size() + node->value().size();
        erase(node, false /* remove_from_map */);
      }
    }
  } else {
    // Erase all same key entries from the list.
    for (auto i = headers_.begin(); i != headers_.end();) {
      if (i->key() == key) {
        removed_bytes += i->key().size() + i->value().size();
        i = erase(i, false /* remove_from_map */);
      } else {
        ++i;
      }
    }
  }
  return removed_bytes;
}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(const LowerCaseString& key) : key_(key) {}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(const LowerCaseString& key, HeaderString&& value)
    : key_(key), value_(std::move(value)) {}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(HeaderString&& key, HeaderString&& value)
    : key_(std::move(key)), value_(std::move(value)) {}

void HeaderMapImpl::HeaderEntryImpl::value(absl::string_view value) { value_.setCopy(value); }

void HeaderMapImpl::HeaderEntryImpl::value(uint64_t value) { value_.setInteger(value); }

void HeaderMapImpl::HeaderEntryImpl::value(const HeaderEntry& header) {
  value(header.value().getStringView());
}

template <> HeaderMapImpl::StaticLookupTable<RequestHeaderMap>::StaticLookupTable() {
#define REGISTER_DEFAULT_REQUEST_HEADER(name)                                                      \
  CustomInlineHeaderRegistry::registerInlineHeader<RequestHeaderMap::header_map_type>(             \
      Headers::get().name);
  INLINE_REQ_HEADERS(REGISTER_DEFAULT_REQUEST_HEADER)
  INLINE_REQ_RESP_HEADERS(REGISTER_DEFAULT_REQUEST_HEADER)

  finalizeTable();

  // Special case where we map a legacy host header to :authority.
  const auto handle =
      CustomInlineHeaderRegistry::getInlineHeader<RequestHeaderMap::header_map_type>(
          Headers::get().Host);
  add(Headers::get().HostLegacy.get().c_str(), [handle](HeaderMapImpl& h) -> StaticLookupResponse {
    return {&h.inlineHeaders()[handle.value().it_->second], &handle.value().it_->first};
  });
}

template <> HeaderMapImpl::StaticLookupTable<RequestTrailerMap>::StaticLookupTable() {
  finalizeTable();
}

template <> HeaderMapImpl::StaticLookupTable<ResponseHeaderMap>::StaticLookupTable() {
#define REGISTER_RESPONSE_HEADER(name)                                                             \
  CustomInlineHeaderRegistry::registerInlineHeader<ResponseHeaderMap::header_map_type>(            \
      Headers::get().name);
  INLINE_RESP_HEADERS(REGISTER_RESPONSE_HEADER)
  INLINE_REQ_RESP_HEADERS(REGISTER_RESPONSE_HEADER)
  INLINE_RESP_HEADERS_TRAILERS(REGISTER_RESPONSE_HEADER)

  finalizeTable();
}

template <> HeaderMapImpl::StaticLookupTable<ResponseTrailerMap>::StaticLookupTable() {
#define REGISTER_RESPONSE_TRAILER(name)                                                            \
  CustomInlineHeaderRegistry::registerInlineHeader<ResponseTrailerMap::header_map_type>(           \
      Headers::get().name);
  INLINE_RESP_HEADERS_TRAILERS(REGISTER_RESPONSE_TRAILER)

  finalizeTable();
}

uint64_t HeaderMapImpl::appendToHeader(HeaderString& header, absl::string_view data,
                                       absl::string_view delimiter) {
  if (data.empty()) {
    return 0;
  }
  uint64_t byte_size = 0;
  if (!header.empty()) {
    header.append(delimiter.data(), delimiter.size());
    byte_size += delimiter.size();
  }
  header.append(data.data(), data.size());
  return data.size() + byte_size;
}

void HeaderMapImpl::updateSize(uint64_t from_size, uint64_t to_size) {
  ASSERT(cached_byte_size_ >= from_size);
  cached_byte_size_ -= from_size;
  cached_byte_size_ += to_size;
}

void HeaderMapImpl::addSize(uint64_t size) { cached_byte_size_ += size; }

void HeaderMapImpl::subtractSize(uint64_t size) {
  ASSERT(cached_byte_size_ >= size);
  cached_byte_size_ -= size;
}

void HeaderMapImpl::copyFrom(HeaderMap& lhs, const HeaderMap& header_map) {
  header_map.iterate([&lhs](const HeaderEntry& header) -> HeaderMap::Iterate {
    // TODO(mattklein123) PERF: Avoid copying here if not necessary.
    HeaderString key_string;
    key_string.setCopy(header.key().getStringView());
    HeaderString value_string;
    value_string.setCopy(header.value().getStringView());

    lhs.addViaMove(std::move(key_string), std::move(value_string));
    return HeaderMap::Iterate::Continue;
  });
}

namespace {

// This is currently only used in tests and is not optimized for performance.
HeaderMap::ConstIterateCb
collectAllHeaders(std::vector<std::pair<absl::string_view, absl::string_view>>* dest) {
  return [dest](const HeaderEntry& header) -> HeaderMap::Iterate {
    dest->push_back(std::make_pair(header.key().getStringView(), header.value().getStringView()));
    return HeaderMap::Iterate::Continue;
  };
};

} // namespace

// This is currently only used in tests and is not optimized for performance.
bool HeaderMapImpl::operator==(const HeaderMap& rhs) const {
  if (size() != rhs.size()) {
    return false;
  }

  std::vector<std::pair<absl::string_view, absl::string_view>> rhs_headers;
  rhs_headers.reserve(rhs.size());
  rhs.iterate(collectAllHeaders(&rhs_headers));

  auto i = headers_.begin();
  auto j = rhs_headers.begin();
  for (; i != headers_.end(); ++i, ++j) {
    if (i->key() != j->first || i->value() != j->second) {
      return false;
    }
  }

  return true;
}

bool HeaderMapImpl::operator!=(const HeaderMap& rhs) const { return !operator==(rhs); }

void HeaderMapImpl::insertByKey(HeaderString&& key, HeaderString&& value) {
  auto lookup = staticLookup(key.getStringView());
  if (lookup.has_value()) {
    key.clear();
    if (*lookup.value().entry_ == nullptr) {
      maybeCreateInline(lookup.value().entry_, *lookup.value().key_, std::move(value));
    } else {
      const uint64_t added_size =
          appendToHeader((*lookup.value().entry_)->value(), value.getStringView());
      addSize(added_size);
      value.clear();
    }
  } else {
    addSize(key.size() + value.size());
    HeaderNode i = headers_.insert(std::move(key), std::move(value));
    i->entry_ = i;
  }
}

void HeaderMapImpl::addViaMove(HeaderString&& key, HeaderString&& value) {
  insertByKey(std::move(key), std::move(value));
}

void HeaderMapImpl::addReference(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString ref_value(value);
  insertByKey(std::move(ref_key), std::move(ref_value));
}

void HeaderMapImpl::addReferenceKey(const LowerCaseString& key, uint64_t value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setInteger(value);
  insertByKey(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::addReferenceKey(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setCopy(value);
  insertByKey(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::addCopy(const LowerCaseString& key, uint64_t value) {
  // In the case that the header is appended, we will perform a needless copy of the key and value.
  // This is done on purpose to keep the code simple and should be rare.
  HeaderString new_key;
  new_key.setCopy(key.get());
  HeaderString new_value;
  new_value.setInteger(value);
  insertByKey(std::move(new_key), std::move(new_value));
  ASSERT(new_key.empty());   // NOLINT(bugprone-use-after-move)
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::addCopy(const LowerCaseString& key, absl::string_view value) {
  // In the case that the header is appended, we will perform a needless copy of the key and value.
  // This is done on purpose to keep the code simple and should be rare.
  HeaderString new_key;
  new_key.setCopy(key.get());
  HeaderString new_value;
  new_value.setCopy(value);
  insertByKey(std::move(new_key), std::move(new_value));
  ASSERT(new_key.empty());   // NOLINT(bugprone-use-after-move)
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
}

void HeaderMapImpl::appendCopy(const LowerCaseString& key, absl::string_view value) {
  // TODO(#9221): converge on and document a policy for coalescing multiple headers.
  auto entry = getExisting(key);
  if (!entry.empty()) {
    const uint64_t added_size = appendToHeader(entry[0]->value(), value);
    addSize(added_size);
  } else {
    addCopy(key, value);
  }
}

void HeaderMapImpl::setReference(const LowerCaseString& key, absl::string_view value) {
  remove(key);
  addReference(key, value);
}

void HeaderMapImpl::setReferenceKey(const LowerCaseString& key, absl::string_view value) {
  remove(key);
  addReferenceKey(key, value);
}

void HeaderMapImpl::setCopy(const LowerCaseString& key, absl::string_view value) {
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.http_set_copy_replace_all_headers")) {
    auto entry = getExisting(key);
    if (!entry.empty()) {
      updateSize(entry[0]->value().size(), value.size());
      entry[0]->value(value);
    } else {
      addCopy(key, value);
    }
  } else {
    remove(key);
    addCopy(key, value);
  }
}

uint64_t HeaderMapImpl::byteSize() const { return cached_byte_size_; }

void HeaderMapImpl::verifyByteSizeInternalForTest() const {
  // Computes the total byte size by summing the byte size of the keys and values.
  uint64_t byte_size = 0;
  for (const HeaderEntryImpl& header : headers_) {
    byte_size += header.key().size();
    byte_size += header.value().size();
  }
  ASSERT(cached_byte_size_ == byte_size);
}

HeaderMap::GetResult HeaderMapImpl::get(const LowerCaseString& key) const {
  return HeaderMap::GetResult(const_cast<HeaderMapImpl*>(this)->getExisting(key));
}

HeaderMap::NonConstGetResult HeaderMapImpl::getExisting(const LowerCaseString& key) {
  // Attempt a trie lookup first to see if the user is requesting an O(1) header. This may be
  // relatively common in certain header matching / routing patterns.
  // TODO(mattklein123): Add inline handle support directly to the header matcher code to support
  // this use case more directly.
  HeaderMap::NonConstGetResult ret;
  auto lookup = staticLookup(key.get());
  if (lookup.has_value()) {
    if (*lookup.value().entry_ != nullptr) {
      ret.push_back(*lookup.value().entry_);
    }
    return ret;
  }

  // If the requested header is not an O(1) header try using the lazy map to
  // search for it instead of iterating the headers list.
  if (headers_.maybeMakeMap()) {
    HeaderList::HeaderLazyMap::iterator iter = headers_.mapFind(key.get());
    if (iter != headers_.mapEnd()) {
      const HeaderList::HeaderNodeVector& v = iter->second;
      ASSERT(!v.empty()); // It's impossible to have a map entry with an empty vector as its value.
      for (const auto& values_it : v) {
        // Convert the iterated value to a HeaderEntry*.
        ret.push_back(&(*values_it));
      }
    }
    return ret;
  }

  // If the requested header is not an O(1) header and the lazy map is not in use, we do a full
  // scan. Doing the trie lookup is wasteful in the miss case, but is present for code consistency
  // with other functions that do similar things.
  for (HeaderEntryImpl& header : headers_) {
    if (header.key() == key.get().c_str()) {
      ret.push_back(&header);
    }
  }

  return ret;
}

void HeaderMapImpl::iterate(HeaderMap::ConstIterateCb cb) const {
  for (const HeaderEntryImpl& header : headers_) {
    if (cb(header) == HeaderMap::Iterate::Break) {
      break;
    }
  }
}

void HeaderMapImpl::iterateReverse(HeaderMap::ConstIterateCb cb) const {
  for (auto it = headers_.rbegin(); it != headers_.rend(); it++) {
    if (cb(*it) == HeaderMap::Iterate::Break) {
      break;
    }
  }
}

void HeaderMapImpl::clear() {
  clearInline();
  headers_.clear();
  cached_byte_size_ = 0;
}

size_t HeaderMapImpl::removeIf(const HeaderMap::HeaderMatchPredicate& predicate) {
  const size_t old_size = headers_.size();
  headers_.removeIf([&predicate, this](const HeaderEntryImpl& entry) {
    const bool to_remove = predicate(entry);
    if (to_remove) {
      // If this header should be removed, make sure any references in the
      // static lookup table are cleared as well.
      auto lookup = staticLookup(entry.key().getStringView());
      if (lookup.has_value()) {
        if (lookup.value().entry_) {
          const uint32_t key_value_size =
              (*lookup.value().entry_)->key().size() + (*lookup.value().entry_)->value().size();
          subtractSize(key_value_size);
          *lookup.value().entry_ = nullptr;
        }
      } else {
        subtractSize(entry.key().size() + entry.value().size());
      }
    }
    return to_remove;
  });
  return old_size - headers_.size();
}

size_t HeaderMapImpl::remove(const LowerCaseString& key) {
  const size_t old_size = headers_.size();
  auto lookup = staticLookup(key.get());
  if (lookup.has_value()) {
    removeInline(lookup.value().entry_);
  } else {
    subtractSize(headers_.remove(key.get()));
  }
  return old_size - headers_.size();
}

size_t HeaderMapImpl::removePrefix(const LowerCaseString& prefix) {
  return HeaderMapImpl::removeIf([&prefix](const HeaderEntry& entry) -> bool {
    return absl::StartsWith(entry.key().getStringView(), prefix.get());
  });
}

void HeaderMapImpl::dumpState(std::ostream& os, int indent_level) const {
  iterate([&os,
           spaces = spacesForLevel(indent_level)](const HeaderEntry& header) -> HeaderMap::Iterate {
    os << spaces << "'" << header.key().getStringView() << "', '" << header.value().getStringView()
       << "'\n";
    return HeaderMap::Iterate::Continue;
  });
}

HeaderMapImpl::HeaderEntryImpl& HeaderMapImpl::maybeCreateInline(HeaderEntryImpl** entry,
                                                                 const LowerCaseString& key) {
  if (*entry) {
    return **entry;
  }

  addSize(key.get().size());
  HeaderNode i = headers_.insert(key);
  i->entry_ = i;
  *entry = &(*i);
  return **entry;
}

HeaderMapImpl::HeaderEntryImpl& HeaderMapImpl::maybeCreateInline(HeaderEntryImpl** entry,
                                                                 const LowerCaseString& key,
                                                                 HeaderString&& value) {
  if (*entry) {
    value.clear();
    return **entry;
  }

  addSize(key.get().size() + value.size());
  HeaderNode i = headers_.insert(key, std::move(value));
  i->entry_ = i;
  *entry = &(*i);
  return **entry;
}

size_t HeaderMapImpl::removeInline(HeaderEntryImpl** ptr_to_entry) {
  if (!*ptr_to_entry) {
    return 0;
  }

  HeaderEntryImpl* entry = *ptr_to_entry;
  const uint64_t size_to_subtract = entry->entry_->key().size() + entry->entry_->value().size();
  subtractSize(size_to_subtract);
  *ptr_to_entry = nullptr;
  headers_.erase(entry->entry_, true);
  return 1;
}

namespace {
template <class T>
HeaderMapImplUtility::HeaderMapImplInfo makeHeaderMapImplInfo(absl::string_view name) {
  // Constructing a header map implementation will force the custom headers and sizing to be
  // finalized, so do that first.
  auto header_map = T::create();

  HeaderMapImplUtility::HeaderMapImplInfo info;
  info.name_ = std::string(name);
  info.size_ = T::inlineHeadersSize() + sizeof(T);
  for (const auto& header : CustomInlineHeaderRegistry::headers<T::header_map_type>()) {
    info.registered_headers_.push_back(header.first.get());
  }
  return info;
}
} // namespace

std::vector<HeaderMapImplUtility::HeaderMapImplInfo>
HeaderMapImplUtility::getAllHeaderMapImplInfo() {
  std::vector<HeaderMapImplUtility::HeaderMapImplInfo> ret;
  ret.push_back(makeHeaderMapImplInfo<RequestHeaderMapImpl>("request header map"));
  ret.push_back(makeHeaderMapImplInfo<RequestTrailerMapImpl>("request trailer map"));
  ret.push_back(makeHeaderMapImplInfo<ResponseHeaderMapImpl>("response header map"));
  ret.push_back(makeHeaderMapImplInfo<ResponseTrailerMapImpl>("response trailer map"));
  return ret;
}

} // namespace Http
} // namespace Envoy
