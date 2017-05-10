#pragma once

#include <regex>

namespace Envoy {
#if __cplusplus < 201103L ||                                                                       \
    (defined(__GLIBCXX__) && (__cplusplus < 201402L) &&                                            \
     (!defined(_GLIBCXX_REGEX_DFS_QUANTIFIERS_LIMIT) && !defined(_GLIBCXX_REGEX_STATE_LIMIT)))
#error "Your compiler does not support std::regex properly.  GCC 4.9+ or Clang required."
#endif
} // Envoy
