#include "common/http/header_map_impl.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "common/common/assert.h"
#include "common/common/dump_state_utils.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"
#include "common/runtime/runtime_features.h"
#include "common/singleton/const_singleton.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Http {

namespace {
constexpr size_t MinDynamicCapacity{32};
// This includes the NULL (StringUtil::itoa technically only needs 21).
constexpr size_t MaxIntegerLength{32};

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

void HeaderString::rtrim() {
  ASSERT(type() == Type::Inline || type() == Type::Dynamic);
  absl::string_view original = getStringView();
  absl::string_view rtrimmed = StringUtil::rtrim(original);
  if (original.size() != rtrimmed.size()) {
    string_length_ = rtrimmed.size();
  }
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
      const uint64_t added_size =
          appendToHeader((*ref_lookup_response.entry_)->value(), value.getStringView());
      addSize(added_size);
      value.clear();
    }
  } else {
    addSize(key.size() + value.size());
    std::list<HeaderEntryImpl>::iterator i = headers_.insert(std::move(key), std::move(value));
    i->entry_ = i;
  }
}

void HeaderMapImpl::addViaMove(HeaderString&& key, HeaderString&& value) {
  // If this is an inline header, we can't addViaMove, because we'll overwrite
  // the existing value.
  auto* entry = getExistingInline(key.getStringView());
  if (entry != nullptr) {
    const uint64_t added_size = appendToHeader(entry->value(), value.getStringView());
    addSize(added_size);
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
    const uint64_t added_size = appendToHeader(entry->value(), buf);
    addSize(added_size);
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
    const uint64_t added_size = appendToHeader(entry->value(), value);
    addSize(added_size);
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

void HeaderMapImpl::appendCopy(const LowerCaseString& key, absl::string_view value) {
  // TODO(#9221): converge on and document a policy for coalescing multiple headers.
  auto entry = getExisting(key);
  if (!entry.empty()) {
    const uint64_t added_size = appendToHeader(entry[0]->value(), value);
    addSize(added_size);
  } else {
    addCopy(key, value);
  }

  verifyByteSize();
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
  verifyByteSize();
}

uint64_t HeaderMapImpl::byteSize() const { return cached_byte_size_; }

uint64_t HeaderMapImpl::byteSizeInternal() const {
  // Computes the total byte size by summing the byte size of the keys and values.
  uint64_t byte_size = 0;
  for (const HeaderEntryImpl& header : headers_) {
    byte_size += header.key().size();
    byte_size += header.value().size();
  }
  return byte_size;
}

const HeaderEntry* HeaderMapImpl::get(const LowerCaseString& key) const {
  const auto result = getAll(key);
  return result.empty() ? nullptr : result[0];
}

HeaderMap::GetResult HeaderMapImpl::getAll(const LowerCaseString& key) const {
  return HeaderMap::GetResult(const_cast<HeaderMapImpl*>(this)->getExisting(key));
}

HeaderMap::NonConstGetResult HeaderMapImpl::getExisting(const LowerCaseString& key) {
  // Attempt a trie lookup first to see if the user is requesting an O(1) header. This may be
  // relatively common in certain header matching / routing patterns.
  // TODO(mattklein123): Add inline handle support directly to the header matcher code to support
  // this use case more directly.
  HeaderMap::NonConstGetResult ret;

  EntryCb cb = ConstSingleton<StaticLookupTable>::get().find(key.get());
  if (cb) {
    StaticLookupResponse ref_lookup_response = cb(*this);
    if (*ref_lookup_response.entry_) {
      ret.push_back(*ref_lookup_response.entry_);
    }
    return ret;
  }
  // If the requested header is not an O(1) header we do a full scan. Doing the trie lookup is
  // wasteful in the miss case, but is present for code consistency with other functions that do
  // similar things.
  // TODO(mattklein123): The full scan here and in remove() are the biggest issues with this
  // implementation for certain use cases. We can either replace this with a totally different
  // implementation or potentially create a lazy map if the size of the map is above a threshold.
  for (HeaderEntryImpl& header : headers_) {
    if (header.key() == key.get().c_str()) {
      ret.push_back(&header);
    }
  }
  return ret;
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
  cached_byte_size_ = 0;
}

void HeaderMapImpl::remove(const LowerCaseString& key) {
  EntryCb cb = ConstSingleton<StaticLookupTable>::get().find(key.get());
  if (cb) {
    StaticLookupResponse ref_lookup_response = cb(*this);
    removeInline(ref_lookup_response.entry_);
  } else {
    for (auto i = headers_.begin(); i != headers_.end();) {
      if (i->key() == key.get().c_str()) {
        subtractSize(i->key().size() + i->value().size());
        i = headers_.erase(i);
      } else {
        ++i;
      }
    }
  }
  verifyByteSize();
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
          subtractSize(key_value_size);
          *ref_lookup_response.entry_ = nullptr;
        }
      } else {
        subtractSize(entry.key().size() + entry.value().size());
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

  addSize(key.get().size());
  std::list<HeaderEntryImpl>::iterator i = headers_.insert(key);
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
  std::list<HeaderEntryImpl>::iterator i = headers_.insert(key, std::move(value));
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
  const uint64_t size_to_subtract = entry->entry_->key().size() + entry->entry_->value().size();
  subtractSize(size_to_subtract);
  *ptr_to_entry = nullptr;
  headers_.erase(entry->entry_);
  verifyByteSize();
}

} // namespace Http
} // namespace Envoy
