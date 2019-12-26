#include <unordered_map>

#include "gtest/gtest.h"
#include "proto_cxx_utils.h"

namespace ApiBooster {
namespace {

// Validate C++ to proto type name conversion.
TEST(ProtoCxxUtils, CxxToProtoType) {
  EXPECT_EQ("", ProtoCxxUtils::cxxToProtoType(""));
  EXPECT_EQ("foo", ProtoCxxUtils::cxxToProtoType("foo"));
  EXPECT_EQ("foo.bar", ProtoCxxUtils::cxxToProtoType("foo::bar"));
  EXPECT_EQ("foo.bar", ProtoCxxUtils::cxxToProtoType("foo::bar::FooCase"));
  EXPECT_EQ("foo.bar.Baz.Blah", ProtoCxxUtils::cxxToProtoType("foo::bar::Baz_Blah"));
}

// Validate proto to C++ type name conversion.
TEST(ProtoCxxUtils, ProtoToCxxType) {
  EXPECT_EQ("", ProtoCxxUtils::protoToCxxType("", false, false));
  EXPECT_EQ("", ProtoCxxUtils::protoToCxxType("", true, false));
  EXPECT_EQ("foo", ProtoCxxUtils::protoToCxxType("foo", false, false));
  EXPECT_EQ("foo", ProtoCxxUtils::protoToCxxType("foo", true, false));
  EXPECT_EQ("bar", ProtoCxxUtils::protoToCxxType("foo.bar", false, false));
  EXPECT_EQ("foo::bar", ProtoCxxUtils::protoToCxxType("foo.bar", true, false));
  EXPECT_EQ("foo::Bar", ProtoCxxUtils::protoToCxxType("foo.Bar", true, false));
  EXPECT_EQ("foo", ProtoCxxUtils::protoToCxxType("foo.Bar", true, true));
  EXPECT_EQ("foo::Bar_Baz", ProtoCxxUtils::protoToCxxType("foo.Bar.Baz", true, false));
  EXPECT_EQ("foo::Bar_Baz_Blah", ProtoCxxUtils::protoToCxxType("foo.Bar.Baz.Blah", true, false));
  EXPECT_EQ("foo::Bar_Baz", ProtoCxxUtils::protoToCxxType("foo.Bar.Baz.Blah", true, true));
}

// Validate proto field accessor upgrades.
TEST(ProtoCxxUtils, RenameMethod) {
  const std::unordered_map<std::string, std::string> field_renames = {
      {"foo", "bar"},
      {"bar", "baz"},
  };
  EXPECT_EQ(absl::nullopt, ProtoCxxUtils::renameMethod("whatevs", field_renames));
  EXPECT_EQ("bar", ProtoCxxUtils::renameMethod("foo", field_renames));
  EXPECT_EQ("baz", ProtoCxxUtils::renameMethod("bar", field_renames));

  EXPECT_EQ("clear_bar", ProtoCxxUtils::renameMethod("clear_foo", field_renames));
  EXPECT_EQ("set_bar", ProtoCxxUtils::renameMethod("set_foo", field_renames));
  EXPECT_EQ("has_bar", ProtoCxxUtils::renameMethod("has_foo", field_renames));
  EXPECT_EQ("mutable_bar", ProtoCxxUtils::renameMethod("mutable_foo", field_renames));
  EXPECT_EQ("set_allocated_bar", ProtoCxxUtils::renameMethod("set_allocated_foo", field_renames));
  EXPECT_EQ("release_bar", ProtoCxxUtils::renameMethod("release_foo", field_renames));
  EXPECT_EQ("add_bar", ProtoCxxUtils::renameMethod("add_foo", field_renames));
  EXPECT_EQ("bar_size", ProtoCxxUtils::renameMethod("foo_size", field_renames));
}

} // namespace
} // namespace ApiBooster
