#include <string>

#include "envoy/common/optref.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(OptRefTest, Empty) {
  OptRef<std::string> optref;
  EXPECT_FALSE(optref.has_value());
  EXPECT_FALSE(optref);
  EXPECT_EQ(optref, absl::nullopt);
  EXPECT_EQ(absl::nullopt, optref);
  absl::optional<std::string> copy = optref.copy();
  EXPECT_FALSE(copy);
  EXPECT_TRUE(!copy);
}

TEST(OptRefTest, NonConst) {
  std::string str("Hello");
  OptRef<std::string> optref(str);
  EXPECT_TRUE(optref.has_value());
  EXPECT_NE(optref, absl::nullopt);
  EXPECT_NE(absl::nullopt, optref);
  EXPECT_EQ("Hello", *optref);
  EXPECT_EQ(5, optref->size());
  optref->append(", World!");
  EXPECT_EQ("Hello, World!", optref.ref());
  EXPECT_EQ("Hello, World!", *optref);
  std::reference_wrapper<std::string> value = optref.value();
  EXPECT_EQ("Hello, World!", value.get());
  optref.reset();
  EXPECT_FALSE(optref);

  // Emplace
  std::string bye("Bye");
  optref.emplace(bye);
  EXPECT_EQ("Bye", *optref);
}

TEST(OptRefTest, ConstOptRef) {
  std::string str("Hello");
  const OptRef<std::string> optref(str);
  EXPECT_TRUE(optref.has_value());
  EXPECT_NE(optref, absl::nullopt);
  EXPECT_NE(absl::nullopt, optref);
  EXPECT_EQ("Hello", *optref);
  EXPECT_EQ(5, optref->size());
  EXPECT_EQ("Hello", optref.ref());
  EXPECT_EQ("Hello", *optref);
  std::reference_wrapper<const std::string> value = optref.value();
  EXPECT_EQ("Hello", value.get());
  absl::optional<std::string> copy = optref.copy();
  EXPECT_TRUE(copy);
  EXPECT_FALSE(!copy);

  // Modifying non-const T from const OptRef.
  *optref += ", World!";
  EXPECT_EQ("Hello, World!", *optref);
}

TEST(OptRefTest, ConstObject) {
  std::string str("Hello");
  const OptRef<const std::string> optref(str);
  EXPECT_TRUE(optref.has_value());
  EXPECT_NE(optref, absl::nullopt);
  EXPECT_NE(absl::nullopt, optref);
  EXPECT_EQ("Hello", *optref);
  EXPECT_EQ(5, optref->size());
  EXPECT_EQ("Hello", optref.ref());
  EXPECT_EQ("Hello", *optref);
  std::reference_wrapper<const std::string> value = optref.value();
  EXPECT_EQ("Hello", value.get());
}

class Foo {};
class Bar : public Foo {};

TEST(OptRefTest, Conversion) {
  Foo foo;
  Bar bar;
  OptRef<Foo> foo_ref(foo);
  OptRef<Bar> bar_ref(bar);

  // Copy construct conversion.
  OptRef<Foo> converted_ref(bar);
  OptRef<Foo> converted_optref(bar_ref);

  // Assignment conversion.
  foo_ref = bar;
  foo_ref = bar_ref;

  // Ref conversion on construction from derived class.
  OptRef<Foo> foo_ref_to_bar = bar_ref;
  EXPECT_EQ(&(*foo_ref_to_bar), &bar);

  // Ref conversion on construction from non-const to const.
  OptRef<const Bar> const_ref_to_bar = bar_ref;
  EXPECT_EQ(&(*const_ref_to_bar), &bar);

  // Ref conversion on construction from derived class of null value.
  OptRef<Bar> bar_null_ref(absl::nullopt);
  OptRef<Foo> foo_ref_to_bar_null = bar_null_ref;
  EXPECT_EQ(foo_ref_to_bar_null, absl::nullopt);
}

TEST(OptRefTest, Size) {
  // Using absl::optional for references costs at least a pointer plus a bool.
  // On most platforms this is 16 bytes, on Windows it's 24.
  absl::optional<std::reference_wrapper<uint64_t>> obj1;
  EXPECT_LT(sizeof(uint64_t*), sizeof(obj1));

  // Using OptRef just costs a pointer.
  OptRef<uint64_t> obj2;
  EXPECT_EQ(sizeof(uint64_t*), sizeof(obj2));
}

} // namespace Envoy
