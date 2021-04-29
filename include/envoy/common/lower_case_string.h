#pragma once

#include <string>
#include <vector>

#include "common/common/assert.h"

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"

namespace Envoy {

// Used by ASSERTs to validate internal consistency. E.g. valid HTTP header keys/values should
// never contain embedded NULLs.
static inline bool validHeaderString(absl::string_view s) {
  // If you modify this list of illegal embedded characters you will probably
  // want to change header_map_fuzz_impl_test at the same time.
  for (const char c : s) {
    switch (c) {
    case '\0':
      FALLTHRU;
    case '\r':
      FALLTHRU;
    case '\n':
      return false;
    default:
      continue;
    }
  }
  return true;
}

/**
 * Wrapper for a lower case string used in header operations to generally avoid needless case
 * insensitive compares.
 */
class LowerCaseString {
public:
  LowerCaseString(LowerCaseString&& rhs) noexcept : string_(std::move(rhs.string_)) {
    ASSERT(valid());
  }
  LowerCaseString& operator=(LowerCaseString&& rhs) noexcept {
    string_ = std::move(rhs.string_);
    ASSERT(valid());
    return *this;
  }

  LowerCaseString(const LowerCaseString& rhs) : string_(rhs.string_) { ASSERT(valid()); }
  LowerCaseString& operator=(const LowerCaseString& rhs) {
    string_ = std::move(rhs.string_);
    ASSERT(valid());
    return *this;
  }

  explicit LowerCaseString(absl::string_view new_string) : string_(new_string) {
    ASSERT(valid());
    lower();
  }

  const std::string& get() const { return string_; }
  bool operator==(const LowerCaseString& rhs) const { return string_ == rhs.string_; }
  bool operator!=(const LowerCaseString& rhs) const { return string_ != rhs.string_; }
  bool operator<(const LowerCaseString& rhs) const { return string_.compare(rhs.string_) < 0; }

  friend std::ostream& operator<<(std::ostream& os, const LowerCaseString& lower_case_string) {
    return os << lower_case_string.string_;
  }

private:
  void lower() {
    std::transform(string_.begin(), string_.end(), string_.begin(), absl::ascii_tolower);
  }
  bool valid() const { return validHeaderString(string_); }

  std::string string_;
};

/**
 * Convenient type for a vector of lower case string and string pair.
 */
using LowerCaseStrPairVector = std::vector<std::pair<const LowerCaseString, const std::string>>;

} // namespace Envoy
