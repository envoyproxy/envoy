#pragma once

#include <sstream>

namespace Envoy {

#define LOG_MEMBER(member) ", " #member ": " << (member)

#define LOG_OPTIONAL_MEMBER(member)                                                                \
  ", " #member ": " << (member.has_value() ? absl::StrCat(member.value()) : "null")

// Macro assumes local member variables
// os (ostream)
// indent_level (int)
#define LOG_DETAILS(member)                                                                        \
  do {                                                                                             \
    os << spaces << #member ": ";                                                                  \
    if (member != nullptr) {                                                                       \
      os << "\n";                                                                                  \
      (member)->logState(os, indent_level + 1);                                                    \
    } else {                                                                                       \
      os << spaces << "null\n";                                                                    \
    }                                                                                              \
  } while (false)

// Return the const char* equivalent of string(level*2, ' '), without dealing
// with string creation overhead. Cap arbitrarily at 6 as we're (hopefully)
// not going to have nested objects deeper than that.
inline const char* spacesForLevel(int level) {
  switch (level) {
  case 0:
    return "";
  case 1:
    return "  ";
  case 2:
    return "    ";
  case 3:
    return "      ";
  case 4:
    return "        ";
  case 5:
    return "          ";
  default:
    return "            ";
  }
  return "";
}

} // namespace Envoy
