#include "source/common/http/header_map_optimized.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Http {

void HeaderMapOptimized::addCopy(const LowerCaseString& key, absl::string_view value) {
  if (wouldExceedLimits(key.get().size(), value.size())) {
    return;
  }

  const absl::string_view key_view = getKeyView(key);
  if (const auto it = index_map_.find(key_view); it != index_map_.end()) {
    // Update existing entry - subtract old value size first
    const size_t old_value_size = entries_[it->second]->value().getStringView().size();
    byte_size_ -= old_value_size;

    entries_[it->second]->setValue(value);
    byte_size_ += value.size();
  } else {
    // Add new entry
    entries_.push_back(std::make_unique<HeaderEntry>(key, value, false));
    // Store a copy of the key string for the map
    index_map_.emplace(std::string(key_view), entries_.size() - 1);
    byte_size_ += key_view.size() + value.size();
  }
}

void HeaderMapOptimized::addReference(const LowerCaseString& key, absl::string_view value) {
  if (wouldExceedLimits(key.get().size(), value.size())) {
    return;
  }

  const absl::string_view key_view = getKeyView(key);
  if (const auto it = index_map_.find(key_view); it != index_map_.end()) {
    // Update existing entry - subtract old value size first
    const size_t old_value_size = entries_[it->second]->value().getStringView().size();
    byte_size_ -= old_value_size;

    entries_[it->second]->setReference(value);
    byte_size_ += value.size();
  } else {
    // Add new entry
    entries_.push_back(std::make_unique<HeaderEntry>(key, value, true));
    // Store a copy of the key string for the map
    index_map_.emplace(std::string(key_view), entries_.size() - 1);
    byte_size_ += key_view.size() + value.size();
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
  const absl::string_view key_view = getKeyView(key);
  if (const auto it = index_map_.find(key_view); it != index_map_.end()) {
    // Append to existing entry
    const absl::string_view old_value = entries_[it->second]->value().getStringView();
    const std::string new_value = absl::StrCat(old_value, value);

    // Check if appending would exceed limits
    if (wouldExceedLimits(0, value.size())) {
      return;
    }

    entries_[it->second]->setValue(new_value);
    byte_size_ += value.size();
  } else {
    // Add new entry (same as addCopy)
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
  const absl::string_view key_view = getKeyView(key);
  if (const auto it = index_map_.find(key_view);
      it != index_map_.end() && it->second < entries_.size()) {
    NonConstGetResult result;
    result.push_back(entries_[it->second].get());
    return GetResult(std::move(result));
  }
  return GetResult(NonConstGetResult{});
}

void HeaderMapOptimized::iterate(const ConstIterateCb cb) const {
  for (const auto& entry : entries_) {
    if (cb(*entry) == Iterate::Break) {
      break;
    }
  }
}

void HeaderMapOptimized::iterateReverse(const ConstIterateCb cb) const {
  for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
    if (cb(**it) == Iterate::Break) {
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
  const absl::string_view key_view = getKeyView(key);
  if (const auto it = index_map_.find(key_view); it != index_map_.end()) {
    const size_t index_to_remove = it->second;

    // Update byte size
    byte_size_ -= key_view.size() + entries_[index_to_remove]->value().getStringView().size();

    // Remove the entry from the index map first
    index_map_.erase(it);

    // Remove the entry from the entry vector
    entries_.erase(entries_.begin() + index_to_remove);

    // Update indices in the index map for all entries that came after the removed one
    for (auto& val : index_map_ | std::views::values) {
      if (val > index_to_remove) {
        val--;
      }
    }

    return 1;
  }
  return 0;
}

size_t HeaderMapOptimized::removeIf(const HeaderMatchPredicate& predicate) {
  // Count how many headers will be removed first
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

  // Build a new header map with only the entries we want to keep
  std::vector<std::unique_ptr<HeaderEntry>> new_entries;
  new_entries.reserve(entries_.size() - removed_count);

  // New index map
  IndexMap new_index_map;
  new_index_map.reserve(entries_.size() - removed_count);

  // Reset byte size and rebuild
  byte_size_ = 0;

  // Copy over entries that don't match the predicate
  for (auto& entry : entries_) {
    if (!predicate(*entry)) {
      // Get key and value views
      const absl::string_view key_view = entry->key().getStringView();
      const absl::string_view value_view = entry->value().getStringView();

      // Create copies to ensure safety
      const std::string key_copy(key_view);
      const std::string value_copy(value_view);

      // Create a new entry with copied data
      auto new_entry = std::make_unique<HeaderEntry>(LowerCaseString(key_copy), value_copy, false);

      // Update byte size
      byte_size_ += key_copy.size() + value_copy.size();

      // Add to an index map and entries
      new_index_map.emplace(key_copy, new_entries.size());
      new_entries.push_back(std::move(new_entry));
    }
  }

  // Replace original entries and index map atomically
  entries_ = std::move(new_entries);
  index_map_ = std::move(new_index_map);

  return removed_count;
}

size_t HeaderMapOptimized::removePrefix(const LowerCaseString& prefix) {
  const absl::string_view prefix_view = getKeyView(prefix);
  const size_t prefix_length = prefix_view.size();

  // Use a lambda that captures the prefix by value to avoid lifetime issues
  return removeIf([prefix_copy = std::string(prefix_view),
                   prefix_length](const Http::HeaderEntry& entry) -> bool {
    const absl::string_view key = entry.key().getStringView();

    // Check if the key is long enough to have the prefix
    if (key.size() < prefix_length) {
      return false;
    }

    // Check if key starts with prefix
    return key.substr(0, prefix_length) == prefix_copy;
  });
}

void HeaderMapOptimized::dumpState(std::ostream& os, const int indent_level) const {
  const char* spaces = "  ";
  for (int i = 0; i < indent_level; i++) {
    os << spaces;
  }

  os << "HeaderMapOptimized " << this << " size=" << size() << " byte_size=" << byte_size_ << "\n";
  for (const auto& header : entries_) {
    for (int i = 0; i < indent_level; i++) {
      os << spaces;
    }
    os << spaces << "'" << header->key().getStringView() << "', '"
       << header->value().getStringView() << "'\n";
  }
}

bool HeaderMapOptimized::wouldExceedLimits(const size_t new_key_size,
                                           const size_t new_value_size) const {
  const size_t new_byte_size = byte_size_ + new_key_size + new_value_size;
  const size_t new_count = entries_.size() + (new_key_size > 0 ? 1 : 0);

  return (max_headers_kb_ != UINT32_MAX && new_byte_size > max_headers_kb_ * 1024) ||
         (max_headers_count_ != UINT32_MAX && new_count > max_headers_count_);
}

bool HeaderMapOptimized::operator==(const HeaderMap& rhs) const {
  if (size() != rhs.size()) {
    return false;
  }

  // Compare each header in rhs against this map
  bool equal = true;
  rhs.iterate([this, &equal](const Http::HeaderEntry& header) -> HeaderMap::Iterate {
    // Get string views and ensure they are valid
    const absl::string_view key_view = header.key().getStringView();
    const absl::string_view value_view = header.value().getStringView();

    // Create safe copies to avoid any potential memory issues
    const std::string safe_key(key_view.data(), key_view.size());
    const std::string safe_value(value_view.data(), value_view.size());

    // Use the safe key copy for lookup
    if (const auto result = get(LowerCaseString(safe_key));
        result.empty() || result[0]->value().getStringView() != safe_value) {
      equal = false;
      return Iterate::Break;
    }
    return Iterate::Continue;
  });
  return equal;
}

void HeaderMapOptimized::addViaMove(HeaderString&& key, HeaderString&& value) {
  const absl::string_view key_view = key.getStringView();
  const absl::string_view value_view = value.getStringView();

  // Convert to LowerCaseString for consistency
  const LowerCaseString lower_key(key_view);
  const absl::string_view final_key_view = getKeyView(lower_key);

  if (const auto it = index_map_.find(final_key_view); it != index_map_.end()) {
    // Update existing entry - check if the value size increase would exceed limits
    const size_t old_value_size = entries_[it->second]->value().getStringView().size();

    // Only check limits if the new value is larger
    if (value_view.size() > old_value_size) {
      const size_t size_increase = value_view.size() - old_value_size;
      if (wouldExceedLimits(0, size_increase)) {
        return;
      }
    }

    byte_size_ -= old_value_size;
    entries_[it->second]->value().setCopy(value_view);
    byte_size_ += value_view.size();
  } else {
    // Check if adding new entry would exceed limits
    if (wouldExceedLimits(final_key_view.size(), value_view.size())) {
      return;
    }

    // Add new entry
    entries_.push_back(std::make_unique<HeaderEntry>(lower_key, value_view, false));
    index_map_.emplace(std::string(final_key_view), entries_.size() - 1);
    byte_size_ += final_key_view.size() + value_view.size();
  }
}

void HeaderMapOptimized::updateByteSize(const LowerCaseString& key, const absl::string_view value,
                                        const bool add) {
  const size_t key_size = getKeyView(key).size();

  if (add) {
    byte_size_ += key_size + value.size();
  } else {
    // For updates, we need to find the old value size
    if (const auto it = index_map_.find(getKeyView(key)); it != index_map_.end()) {
      const size_t old_value_size = entries_[it->second]->value().getStringView().size();
      byte_size_ = byte_size_ - old_value_size + value.size();
    }
  }
}

} // namespace Http
} // namespace Envoy
