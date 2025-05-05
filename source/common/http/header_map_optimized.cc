#include "source/common/http/header_map_optimized.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Http {

void HeaderMapOptimized::addCopy(const LowerCaseString& key, absl::string_view value) {
  if (wouldExceedLimits(key.get().size(), value.size())) {
    return;
  }

  auto it = index_map_.find(key.get());
  if (it != index_map_.end()) {
    // Update existing entry
    entries_[it->second]->setValue(value);
    updateByteSize(key, value, false);
  } else {
    // Add new entry
    entries_.push_back(std::make_unique<HeaderEntry>(key, value, false));
    index_map_[std::string(key.get())] = entries_.size() - 1;
    updateByteSize(key, value, true);
  }
}

void HeaderMapOptimized::addReference(const LowerCaseString& key, absl::string_view value) {
  if (wouldExceedLimits(key.get().size(), value.size())) {
    return;
  }

  auto it = index_map_.find(key.get());
  if (it != index_map_.end()) {
    // Update existing entry
    entries_[it->second]->setReference(value);
    updateByteSize(key, value, false);
  } else {
    // Add new entry
    entries_.push_back(std::make_unique<HeaderEntry>(key, value, true));
    index_map_[std::string(key.get())] = entries_.size() - 1;
    updateByteSize(key, value, true);
  }
}

void HeaderMapOptimized::addReferenceKey(const LowerCaseString& key, uint64_t value) {
  addCopy(key, std::to_string(value));
}

void HeaderMapOptimized::addReferenceKey(const LowerCaseString& key, absl::string_view value) {
  addReference(key, value);
}

void HeaderMapOptimized::addCopy(const LowerCaseString& key, uint64_t value) {
  addCopy(key, std::to_string(value));
}

void HeaderMapOptimized::appendCopy(const LowerCaseString& key, absl::string_view value) {
  auto it = index_map_.find(key.get());
  if (it != index_map_.end()) {
    // Append to existing entry
    std::string new_value = absl::StrCat(entries_[it->second]->value().getStringView(), value);
    entries_[it->second]->setValue(new_value);
    updateByteSize(key, new_value, false);
  } else {
    // Add new entry
    addCopy(key, value);
  }
}

void HeaderMapOptimized::setCopy(const LowerCaseString& key, absl::string_view value) {
  clear();
  addCopy(key, value);
}

void HeaderMapOptimized::setReference(const LowerCaseString& key, absl::string_view value) {
  clear();
  addReference(key, value);
}

void HeaderMapOptimized::setReferenceKey(const LowerCaseString& key, absl::string_view value) {
  setReference(key, value);
}

HeaderMap::GetResult HeaderMapOptimized::get(const LowerCaseString& key) const {
  // Create a safe, fully-initialized copy of the key string
  const std::string safe_key(key.get());

  // Use the safe copy for lookup
  auto it = index_map_.find(safe_key);
  if (it != index_map_.end() && it->second < entries_.size()) {
    HeaderMap::NonConstGetResult result;
    result.push_back(entries_[it->second].get());
    return HeaderMap::GetResult(std::move(result));
  }
  return HeaderMap::GetResult(HeaderMap::NonConstGetResult{});
}

void HeaderMapOptimized::iterate(HeaderMap::ConstIterateCb cb) const {
  for (const auto& entry : entries_) {
    if (cb(*entry) == HeaderMap::Iterate::Break) {
      break;
    }
  }
}

void HeaderMapOptimized::iterateReverse(HeaderMap::ConstIterateCb cb) const {
  for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
    if (cb(**it) == HeaderMap::Iterate::Break) {
      break;
    }
  }
}

void HeaderMapOptimized::clear() {
  entries_.clear();
  index_map_.clear();
  byte_size_ = 0;
}

size_t HeaderMapOptimized::remove(const LowerCaseString& key) {
  auto it = index_map_.find(key.get());
  if (it != index_map_.end()) {
    const size_t index_to_remove = it->second;
    byte_size_ -= key.get().size() + entries_[index_to_remove]->value().getStringView().size();

    // Remove the entry from the index map first
    index_map_.erase(it);

    // Remove the entry from the entries vector
    entries_.erase(entries_.begin() + index_to_remove);

    // Update indices in the index map for all entries that came after the removed one
    for (auto& index_pair : index_map_) {
      if (index_pair.second > index_to_remove) {
        index_pair.second--;
      }
    }

    return 1;
  }
  return 0;
}

size_t HeaderMapOptimized::removeIf(const HeaderMatchPredicate& predicate) {
  // Count how many headers will be removed
  size_t removed_count = 0;
  for (const auto& entry : entries_) {
    if (predicate(*entry)) {
      removed_count++;
    }
  }

  // If nothing to remove, return early
  if (removed_count == 0) {
    return 0;
  }

  // Build a brand new header map with only the entries we want to keep
  std::vector<std::unique_ptr<HeaderEntry>> new_entries;
  new_entries.reserve(entries_.size() - removed_count);

  // New index map
  absl::flat_hash_map<std::string, size_t> new_index_map;

  // Reset byte size
  byte_size_ = 0;

  // Copy over entries that don't match the predicate
  for (auto& entry : entries_) {
    if (!predicate(*entry)) {
      // Copy the key/value strings to ensure safety
      std::string key_copy(entry->key().getStringView());
      std::string value_copy(entry->value().getStringView());

      // Create a new entry
      auto new_entry = std::make_unique<HeaderEntry>(LowerCaseString(key_copy), value_copy, false);

      // Update byte size
      byte_size_ += key_copy.size() + value_copy.size();

      // Add to index map and entries
      new_index_map[key_copy] = new_entries.size();
      new_entries.push_back(std::move(new_entry));
    }
  }

  // Replace original entries and index map
  entries_ = std::move(new_entries);
  index_map_ = std::move(new_index_map);

  return removed_count;
}

size_t HeaderMapOptimized::removePrefix(const LowerCaseString& prefix) {
  // Create a safe, fully-initialized, permanent copy of the prefix
  const std::string prefix_copy = std::string(prefix.get());
  const size_t prefix_length = prefix_copy.size();

  // Use a lambda with captured copy to call removeIf
  return removeIf([prefix_copy, prefix_length](const Http::HeaderEntry& entry) -> bool {
    // Get the header key
    absl::string_view key = entry.key().getStringView();

    // Check prefix against key
    if (key.size() < prefix_length) {
      return false;
    }

    // Compare prefix using substring
    return key.substr(0, prefix_length) == prefix_copy;
  });
}

void HeaderMapOptimized::dumpState(std::ostream& os, int indent_level) const {
  const char* spaces = "  ";
  for (int i = 0; i < indent_level; i++) {
    os << spaces;
  }

  os << "Header map " << this << " size=" << size() << " byte_size=" << byte_size_ << "\n";
  for (const auto& header : entries_) {
    for (int i = 0; i < indent_level; i++) {
      os << spaces;
    }
    os << spaces << "'" << header->key().getStringView() << "', '"
       << header->value().getStringView() << "'\n";
  }
}

void HeaderMapOptimized::updateByteSize(const LowerCaseString& key, absl::string_view value,
                                        bool add) {
  if (add) {
    byte_size_ += key.get().size() + value.size();
  } else {
    // Subtract old value size and add new value size
    auto it = index_map_.find(key.get());
    if (it != index_map_.end()) {
      byte_size_ -= entries_[it->second]->value().getStringView().size();
      byte_size_ += value.size();
    }
  }
}

bool HeaderMapOptimized::exceedsLimits() const {
  return (max_headers_kb_ != UINT32_MAX && byte_size_ > max_headers_kb_ * 1024) ||
         (max_headers_count_ != UINT32_MAX && entries_.size() >= max_headers_count_);
}

bool HeaderMapOptimized::wouldExceedLimits(size_t new_key_size, size_t new_value_size) const {
  const size_t new_byte_size = byte_size_ + new_key_size + new_value_size;
  return (max_headers_kb_ != UINT32_MAX && new_byte_size > max_headers_kb_ * 1024) ||
         (max_headers_count_ != UINT32_MAX && entries_.size() + 1 > max_headers_count_);
}

bool HeaderMapOptimized::operator==(const HeaderMap& rhs) const {
  if (size() != rhs.size()) {
    return false;
  }

  bool equal = true;
  rhs.iterate([this, &equal](const Http::HeaderEntry& header) -> HeaderMap::Iterate {
    // Create a fully initialized copy of the key string
    const std::string safe_key(header.key().getStringView());
    // Create a fully initialized copy of the value string
    const std::string safe_value(header.value().getStringView());

    // Use LowerCaseString constructor with the safe key
    auto result = get(LowerCaseString(safe_key));
    if (result.empty() || result[0]->value().getStringView() != safe_value) {
      equal = false;
      return HeaderMap::Iterate::Break;
    }
    return HeaderMap::Iterate::Continue;
  });
  return equal;
}

void HeaderMapOptimized::addViaMove(HeaderString&& key, HeaderString&& value) {
  if (exceedsLimits()) {
    return;
  }

  LowerCaseString lower_key(key.getStringView());
  auto it = index_map_.find(lower_key.get());
  if (it != index_map_.end()) {
    // Update existing entry
    entries_[it->second]->value().setCopy(value.getStringView());
    updateByteSize(lower_key, entries_[it->second]->value().getStringView(), false);
  } else {
    // Add new entry
    entries_.push_back(std::make_unique<HeaderEntry>(lower_key, value.getStringView(), false));
    index_map_[std::string(lower_key.get())] = entries_.size() - 1;
    updateByteSize(lower_key, value.getStringView(), true);
  }
}

} // namespace Http
} // namespace Envoy
