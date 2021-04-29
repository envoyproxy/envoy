#pragma once

#include <string>
#include <vector>

#include "envoy/http/valid_header.h"

#include "common/common/assert.h"

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"

namespace Envoy {

/*
 * Simple function pointer to validate lower case string. There is currently no need to use
 * std::function.
 */
using LowerCaseStringValidator = bool (*)(absl::string_view);

/**
 * Wrapper for a lower case string used in header operations to generally avoid needless case
 * insensitive compares.
 */
class LowerCaseString {
public:
  LowerCaseString(LowerCaseString&& rhs,
                  LowerCaseStringValidator valid = Http::validHeaderString) noexcept
      : string_(std::move(rhs.string_)) {
    ASSERT(valid != nullptr ? valid(string_) : true);
  }
  LowerCaseString& operator=(LowerCaseString&& rhs) noexcept {
    string_ = std::move(rhs.string_);
    return *this;
  }

  LowerCaseString(const LowerCaseString& rhs,
                  LowerCaseStringValidator valid = Http::validHeaderString)
      : string_(rhs.string_) {
    ASSERT(valid != nullptr ? valid(string_) : true);
  }
  LowerCaseString& operator=(const LowerCaseString& rhs) {
    string_ = rhs.string_;
    return *this;
  }

  explicit LowerCaseString(absl::string_view new_string,
                           LowerCaseStringValidator valid = Http::validHeaderString)
      : string_(new_string) {
    ASSERT(valid != nullptr ? valid(string_) : true);
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

  std::string string_;
};

/**
 * Convenient type for a vector of lower case string and string pair.
 */
using LowerCaseStrPairVector = std::vector<std::pair<const LowerCaseString, const std::string>>;

} // namespace Envoy
