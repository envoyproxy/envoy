#pragma once

#include <sstream>

namespace Envoy {

// A collection of macros for pretty printing objects on fatal error.
// These are fairly ugly in an attempt to maximize the conditions where fatal error logging occurs,
// i.e. under the Envoy signal handler if encountering a crash due to OOM, where allocating more
// memory would likely lead to the crash handler itself causing a subsequent OOM.

#define DUMP_MEMBER(member) ", " #member ": " << (member)
#define DUMP_MEMBER_AS(member, value) ", " #member ": " << (value)

#define DUMP_OPTIONAL_MEMBER(member)                                                               \
  ", " #member ": " << ((member).has_value() ? absl::StrCat((member).value()) : "null")

#define DUMP_NULLABLE_MEMBER(member, value)                                                        \
  ", " #member ": " << ((member) != nullptr ? (value) : "null")

// Macro assumes local member variables
// os (ostream)
// indent_level (int)
// spaces (const char *)
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
// spaces (const char *)
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

#define DUMP_STATE_UNIMPLEMENTED(classname)                                                        \
  const char* spaces = spacesForLevel(indent_level);                                               \
  os << spaces << __FILE__ << ": " << __LINE__ << " " << #classname << " " << this                 \
     << " has not implemented dumpState\n";

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
