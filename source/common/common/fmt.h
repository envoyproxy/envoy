#pragma once

#include "absl/strings/string_view.h"
#include "fmt/format.h"

// NOLINT(namespace-envoy)

namespace fmt {

// Provide an implementation of format_arg for fmt::format that allows absl::string_view to be
// formatted with the same format specifiers available to std::string.
// TODO(zuercher): Once absl::string_view is replaced with std::string_view, this can be removed
// as fmtlib handles std::string_view natively.
template <typename ArgFormatter>
void format_arg(BasicFormatter<char, ArgFormatter>& f, const char*& format_str,
                const absl::string_view sv) {
  BasicStringRef<char> str(sv.data(), sv.size());
  format_str = f.format(format_str, internal::MakeArg<BasicFormatter<char>>(str));
}

} // namespace fmt
