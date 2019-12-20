#pragma once

#include <regex>

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace ApiBooster {

// Protobuf C++ code generation hackery. This is where the utilities that map
// between C++ and protobuf types, enum constants and identifiers live. Most of
// this is heuristic and needs to match whatever the protobuf compiler does.
// TODO(htuch): investigate what can be done to make use of embedded proto
// descriptors in generated stubs to make these utils more robust.
class ProtoCxxUtils {
public:
  // Convert from a C++ type, e.g. foo::bar::v2, to a protobuf type, e.g.
  // foo.bar.v2.
  static std::string cxxToProtoType(const std::string& cxx_type_name) {
    // Convert from C++ to a qualified proto type. This is fairly hacky stuff,
    // we're essentially reversing the conventions that the protobuf C++
    // compiler is using, e.g. replacing _ and :: with . as needed, guessing
    // that a Case suffix implies some enum switching.
    const std::string dotted_path = std::regex_replace(cxx_type_name, std::regex("::"), ".");
    std::vector<std::string> frags = absl::StrSplit(dotted_path, '.');
    for (std::string& frag : frags) {
      if (!frag.empty() && isupper(frag[0])) {
        frag = std::regex_replace(frag, std::regex("_"), ".");
      }
    }
    if (absl::EndsWith(frags.back(), "Case")) {
      frags.pop_back();
    }
    return absl::StrJoin(frags, ".");
  }

  // Convert from a protobuf type, e.g. foo.bar.v2, to a C++ type, e.g.
  // foo::bar::v2.
  static std::string protoToCxxType(const std::string& proto_type_name) {
    // TODO(htuch): add support for recovering foo::bar::Baz_Blah from foo.bar.Baz.Blah.
    return std::regex_replace(proto_type_name, std::regex(R"(\.)"), "::");
  }
};

} // namespace ApiBooster
