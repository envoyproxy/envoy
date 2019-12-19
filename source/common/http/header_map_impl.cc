#include "common/http/header_map_impl.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "common/common/assert.h"
#include "common/common/dump_state_utils.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"
#include "common/singleton/const_singleton.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Http {

namespace {
constexpr size_t MinDynamicCapacity{32};
// This includes the NULL (StringUtil::itoa technically only needs 21).
constexpr size_t MaxIntegerLength{32};
constexpr size_t MapSizeThreshold{0};

uint64_t newCapacity(uint32_t existing_capacity, uint32_t size_to_append) {
  return (static_cast<uint64_t>(existing_capacity) + size_to_append) * 2;
}

void validateCapacity(uint64_t new_capacity) {
  // If the resizing will cause buffer overflow due to hitting uint32_t::max, an OOM is likely
  // imminent. Fast-fail rather than allow a buffer overflow attack (issue #1421)
  RELEASE_ASSERT(new_capacity <= std::numeric_limits<uint32_t>::max(),
                 "Trying to allocate overly large headers.");
  ASSERT(new_capacity >= MinDynamicCapacity);
}

} // namespace

HeaderString::HeaderString() : type_(Type::Inline) {
  buffer_.dynamic_ = inline_buffer_;
  clear();
  static_assert(sizeof(inline_buffer_) >= MaxIntegerLength, "");
  static_assert(MinDynamicCapacity >= MaxIntegerLength, "");
  ASSERT(valid());
}

HeaderString::HeaderString(const LowerCaseString& ref_value) : type_(Type::Reference) {
  buffer_.ref_ = ref_value.get().c_str();
  string_length_ = ref_value.get().size();
  ASSERT(valid());
}

HeaderString::HeaderString(absl::string_view ref_value) : type_(Type::Reference) {
  buffer_.ref_ = ref_value.data();
  string_length_ = ref_value.size();
  ASSERT(valid());
}

HeaderString::HeaderString(HeaderString&& move_value) noexcept {
  type_ = move_value.type_;
  string_length_ = move_value.string_length_;
  switch (move_value.type_) {
  case Type::Reference: {
    buffer_.ref_ = move_value.buffer_.ref_;
    break;
  }
  case Type::Dynamic: {
    // When we move a dynamic header, we switch the moved header back to its default state (inline).
    buffer_.dynamic_ = move_value.buffer_.dynamic_;
    dynamic_capacity_ = move_value.dynamic_capacity_;
    move_value.type_ = Type::Inline;
    move_value.buffer_.dynamic_ = move_value.inline_buffer_;
    move_value.clear();
    break;
  }
  case Type::Inline: {
    buffer_.dynamic_ = inline_buffer_;
    memcpy(inline_buffer_, move_value.inline_buffer_, string_length_);
    move_value.string_length_ = 0;
    break;
  }
  }
  ASSERT(valid());
}

HeaderString::~HeaderString() { freeDynamic(); }

void HeaderString::freeDynamic() {
  if (type_ == Type::Dynamic) {
    free(buffer_.dynamic_);
  }
}

bool HeaderString::valid() const { return validHeaderString(getStringView()); }

void HeaderString::append(const char* data, uint32_t size) {
  switch (type_) {
  case Type::Reference: {
    // Rather than be too clever and optimize this uncommon case, we dynamically
    // allocate and copy.
    type_ = Type::Dynamic;
    const uint64_t new_capacity = newCapacity(string_length_, size);
    if (new_capacity > MinDynamicCapacity) {
      validateCapacity(new_capacity);
      dynamic_capacity_ = new_capacity;
    } else {
      dynamic_capacity_ = MinDynamicCapacity;
    }
    char* buf = static_cast<char*>(malloc(dynamic_capacity_));
    RELEASE_ASSERT(buf != nullptr, "");
    memcpy(buf, buffer_.ref_, string_length_);
    buffer_.dynamic_ = buf;
    break;
  }

  case Type::Inline: {
    const uint64_t new_capacity = static_cast<uint64_t>(size) + string_length_;
    if (new_capacity <= sizeof(inline_buffer_)) {
      // Already inline and the new value fits in inline storage.
      break;
    }

    FALLTHRU;
  }

  case Type::Dynamic: {
    // We can get here either because we didn't fit in inline or we are already dynamic.
    if (type_ == Type::Inline) {
      const uint64_t new_capacity = newCapacity(string_length_, size);
      validateCapacity(new_capacity);
      buffer_.dynamic_ = static_cast<char*>(malloc(new_capacity));
      RELEASE_ASSERT(buffer_.dynamic_ != nullptr, "");
      memcpy(buffer_.dynamic_, inline_buffer_, string_length_);
      dynamic_capacity_ = new_capacity;
      type_ = Type::Dynamic;
    } else {
      if (size + string_length_ > dynamic_capacity_) {
        const uint64_t new_capacity = newCapacity(string_length_, size);
        validateCapacity(new_capacity);

        // Need to reallocate.
        dynamic_capacity_ = new_capacity;
        buffer_.dynamic_ = static_cast<char*>(realloc(buffer_.dynamic_, dynamic_capacity_));
        RELEASE_ASSERT(buffer_.dynamic_ != nullptr, "");
      }
    }
  }
  }
  ASSERT(validHeaderString(absl::string_view(data, size)));
  memcpy(buffer_.dynamic_ + string_length_, data, size);
  string_length_ += size;
}

void HeaderString::clear() {
  switch (type_) {
  case Type::Reference: {
    break;
  }
  case Type::Inline: {
    FALLTHRU;
  }
  case Type::Dynamic: {
    string_length_ = 0;
  }
  }
}

void HeaderString::setCopy(const char* data, uint32_t size) {
  switch (type_) {
  case Type::Reference: {
    // Switch back to inline and fall through.
    type_ = Type::Inline;
    buffer_.dynamic_ = inline_buffer_;

    FALLTHRU;
  }

  case Type::Inline: {
    if (size <= sizeof(inline_buffer_)) {
      // Already inline and the new value fits in inline storage.
      break;
    }

    FALLTHRU;
  }

  case Type::Dynamic: {
    // We can get here either because we didn't fit in inline or we are already dynamic.
    if (type_ == Type::Inline) {
      dynamic_capacity_ = size * 2;
      validateCapacity(dynamic_capacity_);
      buffer_.dynamic_ = static_cast<char*>(malloc(dynamic_capacity_));
      RELEASE_ASSERT(buffer_.dynamic_ != nullptr, "");
      type_ = Type::Dynamic;
    } else {
      if (size > dynamic_capacity_) {
        // Need to reallocate. Use free/malloc to avoid the copy since we are about to overwrite.
        dynamic_capacity_ = size * 2;
        validateCapacity(dynamic_capacity_);
        free(buffer_.dynamic_);
        buffer_.dynamic_ = static_cast<char*>(malloc(dynamic_capacity_));
        RELEASE_ASSERT(buffer_.dynamic_ != nullptr, "");
      }
    }
  }
  }

  memcpy(buffer_.dynamic_, data, size);
  string_length_ = size;
  ASSERT(valid());
}

void HeaderString::setCopy(absl::string_view view) {
  this->setCopy(view.data(), static_cast<uint32_t>(view.size()));
}

void HeaderString::setInteger(uint64_t value) {
  switch (type_) {
  case Type::Reference: {
    // Switch back to inline and fall through.
    type_ = Type::Inline;
    buffer_.dynamic_ = inline_buffer_;

    FALLTHRU;
  }

  case Type::Inline:
    // buffer_.dynamic_ should always point at inline_buffer_ for Type::Inline.
    ASSERT(buffer_.dynamic_ == inline_buffer_);
    FALLTHRU;
  case Type::Dynamic: {
    // Whether dynamic or inline the buffer is guaranteed to be large enough.
    ASSERT(type_ == Type::Inline || dynamic_capacity_ >= MaxIntegerLength);
    // It's safe to use buffer.dynamic_, since buffer.ref_ is union aliased.
    // This better not change without verifying assumptions across this file.
    static_assert(offsetof(Buffer, dynamic_) == offsetof(Buffer, ref_), "");
    string_length_ = StringUtil::itoa(buffer_.dynamic_, 32, value);
  }
  }
}

void HeaderString::setReference(absl::string_view ref_value) {
  freeDynamic();
  type_ = Type::Reference;
  buffer_.ref_ = ref_value.data();
  string_length_ = ref_value.size();
  ASSERT(valid());
}

// Specialization needed for HeaderMapImpl::HeaderList::insert() when key is LowerCaseString.
// A fully specialized template must be defined once in the program, hence this may not be in
// a header file.
template <> bool HeaderMapImpl::HeaderList::isPseudoHeader(const LowerCaseString& key) {
  return key.get().c_str()[0] == ':';
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

#define INLINE_HEADER_STATIC_MAP_ENTRY(name)                                                       \
  add(Headers::get().name.get().c_str(), [](HeaderMapImpl& h) -> StaticLookupResponse {            \
    return {&h.inline_headers_.name##_, &Headers::get().name};                                     \
  });

/**
 * This is the static lookup table that is used to determine whether a header is one of the O(1)
 * headers. This uses a trie for lookup time at most equal to the size of the incoming string.
 */
struct HeaderMapImpl::StaticLookupTable : public TrieLookupTable<EntryCb> {
  StaticLookupTable() {
    ALL_INLINE_HEADERS(INLINE_HEADER_STATIC_MAP_ENTRY)

    // Special case where we map a legacy host header to :authority.
    add(Headers::get().HostLegacy.get().c_str(), [](HeaderMapImpl& h) -> StaticLookupResponse {
      return {&h.inline_headers_.Host_, &Headers::get().Host};
    });
  }
};

void HeaderMapImpl::HeaderList::appendToHeader(HeaderString& header, absl::string_view data,
                                               absl::string_view delimiter) {
  if (data.empty()) {
    return;
  }
  uint64_t byte_size = 0;
  if (!header.empty()) {
    header.append(delimiter.data(), delimiter.size());
    byte_size += delimiter.size();
  }
  header.append(data.data(), data.size());
  addSize(data.size() + byte_size);
}

HeaderMapImpl::HeaderMapImpl() { inline_headers_.clear(); }

HeaderMapImpl::HeaderMapImpl(
    const std::initializer_list<std::pair<LowerCaseString, std::string>>& values)
    : HeaderMapImpl() {
  for (auto& value : values) {
    HeaderString key_string;
    key_string.setCopy(value.first.get().c_str(), value.first.get().size());
    HeaderString value_string;
    value_string.setCopy(value.second.c_str(), value.second.size());
    addViaMove(std::move(key_string), std::move(value_string));
  }
  verifyByteSize();
}

void HeaderMapImpl::HeaderList::updateSize(uint64_t from_size, uint64_t to_size) {
  ASSERT(cached_byte_size_ >= from_size);
  cached_byte_size_ -= from_size;
  cached_byte_size_ += to_size;
}

void HeaderMapImpl::HeaderList::addSize(uint64_t size) { cached_byte_size_ += size; }

void HeaderMapImpl::HeaderList::subtractSize(uint64_t size) {
  ASSERT(cached_byte_size_ >= size);
  cached_byte_size_ -= size;
}

void HeaderMapImpl::copyFrom(const HeaderMap& header_map) {
  header_map.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        // TODO(mattklein123) PERF: Avoid copying here if not necessary.
        HeaderString key_string;
        key_string.setCopy(header.key().getStringView());
        HeaderString value_string;
        value_string.setCopy(header.value().getStringView());

        static_cast<HeaderMapImpl*>(context)->addViaMove(std::move(key_string),
                                                         std::move(value_string));
        return HeaderMap::Iterate::Continue;
      },
      this);
  verifyByteSize();
}

bool HeaderMapImpl::operator==(const HeaderMapImpl& rhs) const {
  if (size() != rhs.size()) {
    return false;
  }

  for (auto i = headers_.begin(), j = rhs.headers_.begin(); i != headers_.end(); ++i, ++j) {
    if (i->key() != j->key().getStringView() || i->value() != j->value().getStringView()) {
      return false;
    }
  }

  return true;
}

bool HeaderMapImpl::operator!=(const HeaderMapImpl& rhs) const { return !operator==(rhs); }

void HeaderMapImpl::insertByKey(HeaderString&& key, HeaderString&& value) {
  EntryCb cb = ConstSingleton<StaticLookupTable>::get().find(key.getStringView());
  if (cb) {
    key.clear();
    StaticLookupResponse ref_lookup_response = cb(*this);
    if (*ref_lookup_response.entry_ == nullptr) {
      maybeCreateInline(ref_lookup_response.entry_, *ref_lookup_response.key_, std::move(value));
    } else {
      headers_.appendToHeader((*ref_lookup_response.entry_)->value(), value.getStringView());
      value.clear();
    }
  } else {
    HeaderNode i = headers_.insert(std::move(key), std::move(value));
    i->entry_ = i;
  }
}

void HeaderMapImpl::addViaMove(HeaderString&& key, HeaderString&& value) {
  // If this is an inline header, we can't addViaMove, because we'll overwrite
  // the existing value.
  auto* entry = getExistingInline(key.getStringView());
  if (entry != nullptr) {
    headers_.appendToHeader(entry->value(), value.getStringView());
    key.clear();
    value.clear();
  } else {
    insertByKey(std::move(key), std::move(value));
  }
  verifyByteSize();
}

void HeaderMapImpl::addReference(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString ref_value(value);
  addViaMove(std::move(ref_key), std::move(ref_value));
  verifyByteSize();
}

void HeaderMapImpl::addReferenceKey(const LowerCaseString& key, uint64_t value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setInteger(value);
  insertByKey(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
  verifyByteSize();
}

void HeaderMapImpl::addReferenceKey(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setCopy(value);
  insertByKey(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
  verifyByteSize();
}

void HeaderMapImpl::addCopy(const LowerCaseString& key, uint64_t value) {
  auto* entry = getExistingInline(key.get());
  if (entry != nullptr) {
    char buf[32];
    StringUtil::itoa(buf, sizeof(buf), value);
    headers_.appendToHeader(entry->value(), buf);
    return;
  }
  HeaderString new_key;
  new_key.setCopy(key.get());
  HeaderString new_value;
  new_value.setInteger(value);
  insertByKey(std::move(new_key), std::move(new_value));
  ASSERT(new_key.empty());   // NOLINT(bugprone-use-after-move)
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
  verifyByteSize();
}

void HeaderMapImpl::addCopy(const LowerCaseString& key, absl::string_view value) {
  auto* entry = getExistingInline(key.get());
  if (entry != nullptr) {
    headers_.appendToHeader(entry->value(), value);
    return;
  }
  HeaderString new_key;
  new_key.setCopy(key.get());
  HeaderString new_value;
  new_value.setCopy(value);
  insertByKey(std::move(new_key), std::move(new_value));
  ASSERT(new_key.empty());   // NOLINT(bugprone-use-after-move)
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
  verifyByteSize();
}

#if 0
void HeaderMapImpl::append(const LowerCaseString& key, absl::string_view value) {
  HeaderLazyMap::iterator iter = headers_.find(key.get());
  if (iter != headers_.findEnd()) {
#if HEADER_MAP_USE_FLAT_HASH_MAP
    HeaderNodeVector& v = iter->second;
    for (HeaderNode node : v) {
      headers_.appendToHeader(node->value(), value);
    }
#endif
#if HEADER_MAP_USE_MULTI_MAP
    do {
      HeaderNode node = iter->second;
      headers_.appendToHeader(node->value(), value);
      ++iter;
    } while (iter != headers_.findEnd() && iter->second->key().getStringView() == key.get());
#endif
  } else {
    addCopy(key, value);
  }
  verifyByteSize();
}
#endif

void HeaderMapImpl::appendCopy(const LowerCaseString& key, absl::string_view value) {
  // TODO(#9221): converge on and document a policy for coalescing multiple headers.
  auto* entry = getExisting(key);
  if (entry) {
    headers_.appendToHeader(entry->value(), value);
  } else {
    addCopy(key, value);
  }

  verifyByteSize();
}

void HeaderMapImpl::setReference(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString ref_value(value);
  remove(key);
  insertByKey(std::move(ref_key), std::move(ref_value));
  verifyByteSize();
}

void HeaderMapImpl::setReferenceKey(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setCopy(value);
  remove(key);
  insertByKey(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
  verifyByteSize();
}

void HeaderMapImpl::setCopy(const LowerCaseString& key, absl::string_view value) {
  // Replaces the first occurrence of a header if it exists, otherwise adds by copy.
  // TODO(#9221): converge on and document a policy for coalescing multiple headers.
  auto* entry = getExisting(key);
  if (entry) {
    headers_.updateSize(entry->value().size(), value.size());
    entry->value(value);
  } else {
    addCopy(key, value);
  }
  verifyByteSize();
}

uint64_t HeaderMapImpl::HeaderList::byteSizeInternal() const {
  // Computes the total byte size by summing the byte size of the keys and values.
  uint64_t byte_size = 0;
  for (const HeaderEntryImpl& header : headers_) {
    byte_size += header.key().size();
    byte_size += header.value().size();
  }
  return byte_size;
}

const HeaderEntry* HeaderMapImpl::get(const LowerCaseString& key) const {
  if (headers_.maybeMakeMap()) {
    HeaderLazyMap::iterator iter = headers_.find(key.get());
    if (iter != headers_.findEnd()) {
#if HEADER_MAP_USE_FLAT_HASH_MAP
# if HEADER_MAP_USE_SLIST
      const HeaderMapCell& cell = iter->second;
      HeaderEntryImpl& header_entry = *cell.node;
# else
      const HeaderNodeVector& v = iter->second;
      ASSERT(!v.empty()); // It's impossible to have a map entry with an empty vector as its value.
      HeaderEntryImpl& header_entry = *v[0];
 endif
#endif
#if HEADER_MAP_USE_MULTI_MAP
      HeaderEntryImpl& header_entry = *(iter->second);
#endif
      return &header_entry;
    }
  } else {
    for (const HeaderEntryImpl& header : headers_) {
      if (header.key() == key.get().c_str()) {
        return &header;
      }
    }
  }
  return nullptr;
}

HeaderEntry* HeaderMapImpl::getExisting(const LowerCaseString& key) {
  return const_cast<HeaderEntry*>(get(key));
}

void HeaderMapImpl::iterate(ConstIterateCb cb, void* context) const {
  for (const HeaderEntryImpl& header : headers_) {
    if (cb(header, context) == HeaderMap::Iterate::Break) {
      break;
    }
  }
}

void HeaderMapImpl::iterateReverse(ConstIterateCb cb, void* context) const {
  for (auto it = headers_.rbegin(); it != headers_.rend(); it++) {
    if (cb(*it, context) == HeaderMap::Iterate::Break) {
      break;
    }
  }
}

HeaderMap::Lookup HeaderMapImpl::lookup(const LowerCaseString& key,
                                        const HeaderEntry** entry) const {
  EntryCb cb = ConstSingleton<StaticLookupTable>::get().find(key.get());
  if (cb) {
    // The accessor callbacks for predefined inline headers take a HeaderMapImpl& as an argument;
    // even though we don't make any modifications, we need to cast_cast in order to use the
    // accessor.
    //
    // Making this work without const_cast would require managing an additional const accessor
    // callback for each predefined inline header and add to the complexity of the code.
    StaticLookupResponse ref_lookup_response = cb(const_cast<HeaderMapImpl&>(*this));
    *entry = *ref_lookup_response.entry_;
    if (*entry) {
      return Lookup::Found;
    } else {
      return Lookup::NotFound;
    }
  } else {
    *entry = nullptr;
    return Lookup::NotSupported;
  }
}

void HeaderMapImpl::clear() {
  inline_headers_.clear();
  headers_.clear();
}

bool HeaderMapImpl::HeaderList::maybeMakeMap() const {
  if (lazy_map_.empty()) {
    if (headers_.size() < MapSizeThreshold) {
      return false;
    }
    for (auto node = headers_.begin(); node != headers_.end(); ++node) {
      absl::string_view key = node->key().getStringView();
#if HEADER_MAP_USE_FLAT_HASH_MAP
      HeaderNodeVector& v = lazy_map_[key];
      v.push_back(node);
#endif
#if HEADER_MAP_USE_MULTI_MAP
      lazy_map_.insert(std::make_pair(key, node));
#endif
    }
  }
  return true;
}

HeaderMapImpl::HeaderLazyMap::iterator
HeaderMapImpl::HeaderList::find(absl::string_view key) const {
  if (lazy_map_.empty()) {
    for (auto node = headers_.begin(); node != headers_.end(); ++node) {
      absl::string_view key = node->key().getStringView();
#if HEADER_MAP_USE_FLAT_HASH_MAP
      HeaderNodeVector& v = lazy_map_[key];
      v.push_back(node);
#endif
#if HEADER_MAP_USE_MULTI_MAP
      lazy_map_.insert(std::make_pair(key, node));
#endif
    }
  }
  return lazy_map_.find(key);
}

void HeaderMapImpl::remove(const LowerCaseString& key) {
  EntryCb cb = ConstSingleton<StaticLookupTable>::get().find(key.get());
  if (cb) {
    StaticLookupResponse ref_lookup_response = cb(*this);
    removeInline(ref_lookup_response.entry_);
  } else {
    headers_.remove(key.get());
  }
}

void HeaderMapImpl::HeaderList::remove(absl::string_view key) {
  if (maybeMakeMap()) {
    auto iter = find(key);
    if (iter != lazy_map_.end()) {
#if HEADER_MAP_USE_FLAT_HASH_MAP
# if HEADER_MAP_USE_SLIST
      HeaderCell* cell = &iter->second;
      do {
        const HeaderNode& node = cell->node;
        ASSERT(node->key() == key);
        erase(node, false /* clear_from_map */);
        cell = cell->next;
      } while (cell != nullptr);
      lazy_map_.erase(iter);
# else
      HeaderNodeVector header_nodes = std::move(iter->second);
      lazy_map_.erase(iter);
      for (const HeaderNode& node : header_nodes) {
        ASSERT(node->key() == key);
        erase(node, false /* clear_from_map */);
      }
# endif
#endif
#if HEADER_MAP_USE_MULTI_MAP
      ASSERT(iter->second->key() == key);
      do {
        auto iter_to_erase = iter;
        ++iter;
        HeaderNode& node = iter_to_erase->second;
        erase(node, false /* clear_from_map */);
        lazy_map_.erase(iter_to_erase);
      } while (iter != lazy_map_.end() && iter->second->key() == key);
#endif
    }
  } else {
    for (HeaderNode i = headers_.begin(); i != headers_.end();) {
      if (i->key() == key) {
        i = erase(i, false);
      } else {
        ++i;
      }
    }
  }
}

HeaderMapImpl::HeaderNode HeaderMapImpl::HeaderList::erase(HeaderNode i, bool clear_from_map) {
  if (pseudo_headers_end_ == i) {
    pseudo_headers_end_++;
  }
  subtractSize(i->key().size() + i->value().size());
  if (clear_from_map) {
    lazy_map_.erase(i->key().getStringView());
  }
  return headers_.erase(i);
}

void HeaderMapImpl::removePrefix(const LowerCaseString& prefix) {
  headers_.remove_if([&prefix, this](const HeaderEntryImpl& entry) {
    bool to_remove = absl::StartsWith(entry.key().getStringView(), prefix.get());
    if (to_remove) {
      // If this header should be removed, make sure any references in the
      // static lookup table are cleared as well.
      EntryCb cb = ConstSingleton<StaticLookupTable>::get().find(entry.key().getStringView());
      if (cb) {
        StaticLookupResponse ref_lookup_response = cb(*this);
        if (ref_lookup_response.entry_) {
          const uint32_t key_value_size = (*ref_lookup_response.entry_)->key().size() +
                                          (*ref_lookup_response.entry_)->value().size();
          headers_.subtractSize(key_value_size);
          *ref_lookup_response.entry_ = nullptr;
        }
      } else {
        headers_.subtractSize(entry.key().size() + entry.value().size());
      }
    }
    return to_remove;
  });
  verifyByteSize();
}

void HeaderMapImpl::dumpState(std::ostream& os, int indent_level) const {
  using IterateData = std::pair<std::ostream*, const char*>;
  const char* spaces = spacesForLevel(indent_level);
  IterateData iterate_data = std::make_pair(&os, spaces);
  iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        auto* data = static_cast<IterateData*>(context);
        *data->first << data->second << "'" << header.key().getStringView() << "', '"
                     << header.value().getStringView() << "'\n";
        return HeaderMap::Iterate::Continue;
      },
      &iterate_data);
}

HeaderMapImpl::HeaderEntryImpl& HeaderMapImpl::maybeCreateInline(HeaderEntryImpl** entry,
                                                                 const LowerCaseString& key) {
  if (*entry) {
    return **entry;
  }

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

  HeaderNode i = headers_.insert(key, std::move(value));
  i->entry_ = i;
  *entry = &(*i);
  return **entry;
}

HeaderMapImpl::HeaderEntryImpl* HeaderMapImpl::getExistingInline(absl::string_view key) {
  EntryCb cb = ConstSingleton<StaticLookupTable>::get().find(key);
  if (cb) {
    StaticLookupResponse ref_lookup_response = cb(*this);
    return *ref_lookup_response.entry_;
  }
  return nullptr;
}

void HeaderMapImpl::removeInline(HeaderEntryImpl** ptr_to_entry) {
  if (!*ptr_to_entry) {
    return;
  }

  HeaderEntryImpl* entry = *ptr_to_entry;
  *ptr_to_entry = nullptr;
  headers_.erase(entry->entry_, true);
  verifyByteSize();
}

} // namespace Http
} // namespace Envoy
