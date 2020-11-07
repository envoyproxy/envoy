#pragma once

#include <regex>

#include "absl/container/node_hash_map.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/types/optional.h"

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
  static std::string cxxToProtoType(const std::string& cxx_type_name);

  // Given a method, e.g. mutable_foo, rele, and a map of renames in a give proto,
  // determine if the method is covered by a generated C++ stub for a renamed
  // field in proto, and if so, return the new method name.
  static absl::optional<std::string>
  renameMethod(absl::string_view method_name,
               const absl::node_hash_map<std::string, std::string> renames);

  // Given a constant, e.g. kFooBar, determine if it needs upgrading. We need
  // this for synthesized oneof cases.
  static absl::optional<std::string>
  renameConstant(absl::string_view constant_name,
                 const absl::node_hash_map<std::string, std::string> renames);

  // Given an enum value, e.g. FOO_BAR determine if it needs upgrading.
  static absl::optional<std::string>
  renameEnumValue(absl::string_view enum_value_name,
                  const absl::node_hash_map<std::string, std::string> renames);

  // Convert from a protobuf type, e.g. foo.bar.v2, to a C++ type, e.g.
  // foo::bar::v2.
  static std::string protoToCxxType(const std::string& proto_type_name, bool qualified,
                                    bool enum_type);
};

} // namespace ApiBooster
