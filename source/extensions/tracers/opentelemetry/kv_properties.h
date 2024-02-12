#pragma once

// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0
//
// Originally taken from the official OpenTelemetry C++ API/SDK
// https://github.com/open-telemetry/opentelemetry-cpp/blob/v1.13.0/api/include/opentelemetry/common/kv_properties.h

#include <memory>
#include <string>

#include "source/common/common/utility.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
namespace {

absl::string_view trim(absl::string_view str, size_t left, size_t right) {
  while (left <= right && str[static_cast<std::size_t>(left)] == ' ') {
    left++;
  }
  while (left <= right && str[static_cast<std::size_t>(right)] == ' ') {
    right--;
  }
  return str.substr(left, 1 + right - left);
}

} // namespace

// Constructor parameter for KeyValueStringTokenizer
struct KeyValueStringTokenizerOptions {
  char member_separator = ',';
  char key_value_separator = '=';
  bool ignore_empty_members = true;
};

// Tokenizer for key-value headers
class KeyValueStringTokenizer {
public:
  KeyValueStringTokenizer(absl::string_view str, const KeyValueStringTokenizerOptions& opts =
                                                     KeyValueStringTokenizerOptions()) noexcept
      : str_(str), opts_(opts) {}

  static absl::string_view getDefaultKeyOrValue() {
    static std::string default_str = "";
    return default_str;
  }

  // Returns next key value in the string header
  // @param valid_kv : if the found kv pair is valid or not
  // @param key : key in kv pair
  // @param key : value in kv pair
  // @returns true if next kv pair was found, false otherwise.
  bool next(bool& valid_kv, absl::string_view& key, absl::string_view& value) noexcept {
    valid_kv = true;
    while (index_ < str_.size()) {
      bool is_empty_pair = false;
      size_t end = str_.find(opts_.member_separator, index_);
      if (end == std::string::npos) {
        end = str_.size() - 1;
      } else if (end == index_) // empty pair. do not update end
      {
        is_empty_pair = true;
      } else {
        end--;
      }

      auto list_member = trim(str_, index_, end);
      if (list_member.empty() || is_empty_pair) {
        // empty list member
        index_ = end + 2 - is_empty_pair;
        if (opts_.ignore_empty_members) {
          continue;
        }

        valid_kv = true;
        key = getDefaultKeyOrValue();
        value = getDefaultKeyOrValue();
        return true;
      }

      auto key_end_pos = list_member.find(opts_.key_value_separator);
      if (key_end_pos == std::string::npos) {
        // invalid member
        valid_kv = false;
      } else {
        key = list_member.substr(0, key_end_pos);
        value = list_member.substr(key_end_pos + 1);
      }

      index_ = end + 2;

      return true;
    }

    // no more entries remaining
    return false;
  }

  // Returns total number of tokens in header string
  size_t numTokens() const noexcept {
    size_t cnt = 0, begin = 0;
    while (begin < str_.size()) {
      ++cnt;
      size_t end = str_.find(opts_.member_separator, begin);
      if (end == std::string::npos) {
        break;
      }

      begin = end + 1;
    }

    return cnt;
  }

  // Resets the iterator
  void reset() noexcept { index_ = 0; }

private:
  absl::string_view str_;
  KeyValueStringTokenizerOptions opts_;
  size_t index_{};
};

// Class to store fixed size array of key-value pairs of string type
class KeyValueProperties {
  // Class to store key-value pairs of string types
public:
  class Entry {
  public:
    Entry() : key_(nullptr), value_(nullptr) {}

    // Copy constructor
    Entry(const Entry& copy) {
      key_ = copyStringToPointer(copy.key_.get());
      value_ = copyStringToPointer(copy.value_.get());
    }

    // Copy assignment operator
    Entry& operator=(Entry& other) {
      key_ = copyStringToPointer(other.key_.get());
      value_ = copyStringToPointer(other.value_.get());
      return *this;
    }

    // Move contructor and assignment operator
    Entry(Entry&& other) = default;
    Entry& operator=(Entry&& other) = default;

    // Creates an Entry for a given key-value pair.
    Entry(absl::string_view key, absl::string_view value) {
      key_ = copyStringToPointer(key);
      value_ = copyStringToPointer(value);
    }

    // Gets the key associated with this entry.
    absl::string_view getKey() const noexcept { return key_.get(); }

    // Gets the value associated with this entry.
    absl::string_view getValue() const noexcept { return value_.get(); }

    // Sets the value for this entry. This overrides the previous value.
    void setValue(absl::string_view value) noexcept { value_ = copyStringToPointer(value); }

  private:
    // Store key and value as raw char pointers to avoid using std::string.
    std::unique_ptr<const char[]> key_;
    std::unique_ptr<const char[]> value_;

    // Copies string into a buffer and returns a unique_ptr to the buffer.
    // This is a workaround for the fact that memcpy doesn't accept a const destination.
    std::unique_ptr<const char[]> copyStringToPointer(absl::string_view str) {
      char* temp = new char[str.size() + 1];
      memcpy(temp, str.data(), str.size()); // NOLINT(safe-memcpy)
      temp[str.size()] = '\0';
      return std::unique_ptr<const char[]>(temp);
    }
  };

  // Maintain the number of entries in entries_.
  size_t num_entries_;

  // Max size of allocated array
  size_t max_num_entries_;

  // Store entries in a C-style array to avoid using std::array or std::vector.
  std::unique_ptr<Entry[]> entries_;

public:
  // Create Key-value list of given size
  // @param size : Size of list.
  KeyValueProperties(size_t size) noexcept
      : num_entries_(0), max_num_entries_(size), entries_(new Entry[size]) {}

  // Create Empty Key-Value list
  KeyValueProperties() noexcept : num_entries_(0), max_num_entries_(0), entries_(nullptr) {}

  // Adds new kv pair into kv properties
  void addEntry(absl::string_view key, absl::string_view value) noexcept {
    if (num_entries_ < max_num_entries_) {
      Entry entry(key, value);
      (entries_.get())[num_entries_++] = std::move(entry);
    }
  }

  // Returns all kv pair entries
  bool
  getAllEntries(std::function<bool(absl::string_view, absl::string_view)> callback) const noexcept {
    for (size_t i = 0; i < num_entries_; i++) {
      auto& entry = (entries_.get())[i];
      if (!callback(entry.getKey(), entry.getValue())) {
        return false;
      }
    }
    return true;
  }

  // Return value for key if exists, return false otherwise
  bool getValue(absl::string_view key, std::string& value) const noexcept {
    for (size_t i = 0; i < num_entries_; i++) {
      auto& entry = (entries_.get())[i];
      if (entry.getKey() == key) {
        const auto& entry_value = entry.getValue();
        value = std::string(entry_value.data(), entry_value.size());
        return true;
      }
    }
    return false;
  }

  size_t size() const noexcept { return num_entries_; }
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
