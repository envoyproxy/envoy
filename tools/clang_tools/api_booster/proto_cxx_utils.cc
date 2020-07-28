#include "proto_cxx_utils.h"

namespace ApiBooster {

std::string ProtoCxxUtils::cxxToProtoType(const std::string& cxx_type_name) {
  // Convert from C++ to a qualified proto type. This is fairly hacky stuff,
  // we're essentially reversing the conventions that the protobuf C++
  // compiler is using, e.g. replacing _ and :: with . as needed, guessing
  // that a Case suffix implies some enum switching.
  const std::string rel_cxx_type_name =
      absl::StartsWith(cxx_type_name, "::") ? cxx_type_name.substr(2) : cxx_type_name;
  std::vector<std::string> frags = absl::StrSplit(rel_cxx_type_name, "::");
  // TODO(htuch): if we add some more stricter checks on mangled name usage in
  // check_format.py, we should be able to eliminate this.
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

std::string ProtoCxxUtils::protoToCxxType(const std::string& proto_type_name, bool qualified,
                                          bool enum_type) {
  std::vector<std::string> frags = absl::StrSplit(proto_type_name, '.');
  // We drop the enum type name, it's not needed and confuses the mangling
  // when enums are nested in messages.
  if (enum_type) {
    frags.pop_back();
  }
  if (qualified) {
    return absl::StrJoin(frags, "::");
  } else {
    return frags.back();
  }
}

absl::optional<std::string>
ProtoCxxUtils::renameMethod(absl::string_view method_name,
                            const absl::node_hash_map<std::string, std::string> renames) {
  // Simple O(N * M) match, where M is constant (the set of prefixes/suffixes) so
  // should be fine.
  for (const auto& field_rename : renames) {
    const std::vector<std::string> GeneratedMethodPrefixes = {
        "clear_", "set_", "has_", "mutable_", "set_allocated_", "release_", "add_", "",
    };
    // Most of the generated methods are some prefix.
    for (const std::string& prefix : GeneratedMethodPrefixes) {
      if (method_name == prefix + field_rename.first) {
        return prefix + field_rename.second;
      }
    }
    // _size is the only suffix.
    if (method_name == field_rename.first + "_size") {
      return field_rename.second + "_size";
    }
  }
  return {};
}

absl::optional<std::string>
ProtoCxxUtils::renameConstant(absl::string_view constant_name,
                              const absl::node_hash_map<std::string, std::string> renames) {
  if (constant_name.size() < 2 || constant_name[0] != 'k' || !isupper(constant_name[1])) {
    return {};
  }
  std::vector<std::string> frags;
  for (const char c : constant_name.substr(1)) {
    if (isupper(c)) {
      frags.emplace_back(1, tolower(c));
    } else {
      frags.back().push_back(c);
    }
  }
  const std::string field_name = absl::StrJoin(frags, "_");
  const auto it = renames.find(field_name);
  if (it == renames.cend()) {
    return {};
  }
  std::vector<std::string> new_frags = absl::StrSplit(it->second, '_');
  for (auto& frag_it : new_frags) {
    if (!frag_it.empty()) {
      frag_it[0] = toupper(frag_it[0]);
    }
  }
  return "k" + absl::StrJoin(new_frags, "");
}

absl::optional<std::string>
ProtoCxxUtils::renameEnumValue(absl::string_view enum_value_name,
                               const absl::node_hash_map<std::string, std::string> renames) {
  const auto it = renames.find(std::string(enum_value_name));
  if (it == renames.cend()) {
    return {};
  }
  return it->second;
}

} // namespace ApiBooster
