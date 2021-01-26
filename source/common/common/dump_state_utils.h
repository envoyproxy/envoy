#pragma once

#include <sstream>

namespace Envoy {

// A collection of macros for pretty printing objects on fatal error.
// These are fairly ugly in an attempt to maximize the conditions where fatal error logging occurs,
// i.e. under the Envoy signal handler if encountering a crash due to OOM, where allocating more
// memory would likely lead to the crash handler itself causing a subsequent OOM.

#define _DUMP_MEMBER(member) ", " #member ": " << (member)
#define _DUMP_MEMBER_VIA_VALUE(member, value) ", " #member ": " << (value)
#define _DUMP_MEMBER_SELECTOR(_1, _2, DUMP_MACRO, ...) DUMP_MACRO

// This is a workaround for fact that MSVC expands __VA_ARGS__ after passing them into a macro,
// rather than before passing them into a macro. Without this,
// _DUMP_MEMBER_SELECTOR does not work correctly when compiled with MSVC.
#define EXPAND(X) X

// If DUMP_MEMBER is called with one argument, then _DUMP_MEMBER is called.
// If DUMP_MEMBER is called with two arguments, then _DUMP_MEMBER_VIA_VALUE is called.
#define DUMP_MEMBER(...)                                                                           \
  EXPAND(_DUMP_MEMBER_SELECTOR(__VA_ARGS__, _DUMP_MEMBER_VIA_VALUE, _DUMP_MEMBER)(__VA_ARGS__))

#define DUMP_OPTIONAL_MEMBER(member)                                                               \
  ", " #member ": " << ((member).has_value() ? absl::StrCat((member).value()) : "null")

#define DUMP_NULLABLE_MEMBER(member, value)                                                        \
  ", " #member ": " << ((member) != nullptr ? (value) : "null")

// Macro assumes local member variables
// os (ostream)
// indent_level (int)
#define DUMP_DETAILS(member)                                                                       \
  do {                                                                                             \
    os << spaces << #member ": ";                                                                  \
    if (member) {                                                                                  \
      os << "\n";                                                                                  \
      (member)->dumpState(os, indent_level + 1);                                                   \
    } else {                                                                                       \
      os << spaces << "null\n";                                                                    \
    }                                                                                              \
  } while (false)

// Macro assumes local member variables
// os (ostream)
// indent_level (int)
#define DUMP_OPT_REF_DETAILS(member)                                                               \
  do {                                                                                             \
    os << spaces << #member ": ";                                                                  \
    if ((member).has_value()) {                                                                    \
      os << "\n";                                                                                  \
      (member)->get().dumpState(os, indent_level + 1);                                             \
    } else {                                                                                       \
      os << spaces << "empty\n";                                                                   \
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
