#pragma once

#include <string>

#include "common/common/assert.h"

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"

namespace Envoy {

/**
 * Wrapper for case insensitive string to generally avoid needless case insensitive compares. The
 * wrapper will uniformly convert the string content to lowercase.
 */
class LowerCaseStrBase {
public:
  explicit LowerCaseStrBase(absl::string_view new_string) : string_(new_string) { lower(); }

  LowerCaseStrBase(LowerCaseStrBase&&) = default;
  LowerCaseStrBase& operator=(LowerCaseStrBase&&) = default;

  LowerCaseStrBase(const LowerCaseStrBase&) = default;
  LowerCaseStrBase& operator=(const LowerCaseStrBase&) = default;

  const std::string& get() const { return string_; }
  std::string& get() { return string_; }

  bool operator==(const LowerCaseStrBase& rhs) const { return string_ == rhs.string_; }
  bool operator!=(const LowerCaseStrBase& rhs) const { return string_ != rhs.string_; }
  bool operator<(const LowerCaseStrBase& rhs) const { return string_.compare(rhs.string_) < 0; }

  friend std::ostream& operator<<(std::ostream& os, const LowerCaseStrBase& lower_case_string) {
    return os << lower_case_string.string_;
  }

protected:
  void lower() {
    std::transform(string_.begin(), string_.end(), string_.begin(), absl::ascii_tolower);
  }

  std::string string_;
};

template <bool (*V)(absl::string_view)> class ValidatedLowerCaseStr : public LowerCaseStrBase {
public:
  ValidatedLowerCaseStr(LowerCaseStrBase&& rhs) noexcept : LowerCaseStrBase(std::move(rhs)) {
    ASSERT(valid());
  }
  ValidatedLowerCaseStr& operator=(LowerCaseStrBase&& rhs) noexcept {
    string_ = std::move(rhs.get());
    ASSERT(valid());
    return *this;
  }

  ValidatedLowerCaseStr(const LowerCaseStrBase& rhs) : LowerCaseStrBase(rhs) { ASSERT(valid()); }
  ValidatedLowerCaseStr& operator=(const LowerCaseStrBase& rhs) {
    string_ = rhs.get();
    ASSERT(valid());
    return *this;
  }

  explicit ValidatedLowerCaseStr(absl::string_view new_string) : LowerCaseStrBase(new_string) {
    ASSERT(valid());
  }

private:
  bool valid() const { return V(string_); }
};

} // namespace Envoy
