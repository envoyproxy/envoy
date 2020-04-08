/**
 * IMPORTANT: this file is a fork of the soon to be open-source absl::StatusOr class.
 * When the absl::StatusOr lands this file will be removed.
 */

/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

// Unit tests for StatusOr

#include <memory>
#include <type_traits>

#include "third_party/statusor/statusor.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace absl {
namespace {


class Base1 {
 public:
  virtual ~Base1() {}
  int pad_;
};

class Base2 {
 public:
  virtual ~Base2() {}
  int yetotherpad_;
};

class Derived : public Base1, public Base2 {
 public:
  ~Derived() override {}
  int evenmorepad_;
};

class CopyNoAssign {
 public:
  explicit CopyNoAssign(int value) : foo_(value) {}
  CopyNoAssign(const CopyNoAssign& other) : foo_(other.foo_) {}
  int foo_;

 private:
  const CopyNoAssign& operator=(const CopyNoAssign&);
};

class NoDefaultConstructor {
 public:
  explicit NoDefaultConstructor(int foo);
};

static_assert(!std::is_default_constructible<NoDefaultConstructor>(),
              "Should not be default-constructible.");

StatusOr<std::unique_ptr<int>> ReturnUniquePtr() {
  // Uses implicit constructor from T&&
  return std::unique_ptr<int>(new int(0));
}

TEST(StatusOr, ElementType) {
  static_assert(std::is_same<StatusOr<int>::element_type, int>(), "");
  static_assert(std::is_same<StatusOr<char>::element_type, char>(), "");
}

TEST(StatusOr, NullPointerStatusOr) {
  // As a very special case, null-plain-pointer StatusOr used to be an
  // error. Test that it no longer is.
  StatusOr<int*> null_status(nullptr);
  EXPECT_TRUE(null_status.ok());
  EXPECT_EQ(null_status.value(), nullptr);
}

TEST(StatusOr, TestNoDefaultConstructorInitialization) {
  // Explicitly initialize it with an error code.
  StatusOr<NoDefaultConstructor> statusor(absl::CancelledError(""));
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(statusor.status().code(), absl::StatusCode::kCancelled);

  // Default construction of StatusOr initializes it with an UNKNOWN error code.
  StatusOr<NoDefaultConstructor> statusor2;
  EXPECT_FALSE(statusor2.ok());
  EXPECT_EQ(statusor2.status().code(), absl::StatusCode::kUnknown);
}

TEST(StatusOr, TestMoveOnlyInitialization) {
  StatusOr<std::unique_ptr<int>> thing(ReturnUniquePtr());
  ASSERT_TRUE(thing.ok());
  EXPECT_EQ(0, *thing.value());
  int* previous = thing.value().get();

  thing = ReturnUniquePtr();
  EXPECT_TRUE(thing.ok());
  EXPECT_EQ(0, *thing.value());
  EXPECT_NE(previous, thing.value().get());
}

TEST(StatusOr, TestMoveOnlyStatusCtr) {
  StatusOr<std::unique_ptr<int>> thing(absl::CancelledError(""));
  ASSERT_FALSE(thing.ok());
}

TEST(StatusOr, TestMoveOnlyValueExtraction) {
  StatusOr<std::unique_ptr<int>> thing(ReturnUniquePtr());
  ASSERT_TRUE(thing.ok());
  std::unique_ptr<int> ptr = std::move(thing).value();
  EXPECT_EQ(0, *ptr);

  thing = std::move(ptr);
  ptr = std::move(thing.value());
  EXPECT_EQ(0, *ptr);
}

TEST(StatusOr, TestMoveOnlyConversion) {
  StatusOr<std::unique_ptr<const int>> const_thing(ReturnUniquePtr());
  EXPECT_TRUE(const_thing.ok());
  EXPECT_EQ(0, *const_thing.value());

  // Test rvalue converting assignment
  const int* const_previous = const_thing.value().get();
  const_thing = ReturnUniquePtr();
  EXPECT_TRUE(const_thing.ok());
  EXPECT_EQ(0, *const_thing.value());
  EXPECT_NE(const_previous, const_thing.value().get());
}

TEST(StatusOr, TestMoveOnlyVector) {
  // Sanity check that StatusOr<MoveOnly> works in vector.
  std::vector<StatusOr<std::unique_ptr<int>>> vec;
  vec.push_back(ReturnUniquePtr());
  vec.resize(2);
  auto another_vec = std::move(vec);
  EXPECT_EQ(0, *another_vec[0].value());
  EXPECT_EQ(absl::StatusCode::kUnknown, another_vec[1].status().code());
}

TEST(StatusOr, TestMoveWithValuesAndErrors) {
  StatusOr<std::string> status_or(std::string(1000, '0'));
  StatusOr<std::string> value1(std::string(1000, '1'));
  StatusOr<std::string> value2(std::string(1000, '2'));
  StatusOr<std::string> error1(Status(absl::StatusCode::kUnknown, "error1"));
  StatusOr<std::string> error2(Status(absl::StatusCode::kUnknown, "error2"));

  ASSERT_TRUE(status_or.ok());
  EXPECT_EQ(std::string(1000, '0'), status_or.value());

  // Overwrite the value in status_or with another value.
  status_or = std::move(value1);
  ASSERT_TRUE(status_or.ok());
  EXPECT_EQ(std::string(1000, '1'), status_or.value());

  // Overwrite the value in status_or with an error.
  status_or = std::move(error1);
  ASSERT_FALSE(status_or.ok());
  EXPECT_EQ("error1", status_or.status().message());

  // Overwrite the error in status_or with another error.
  status_or = std::move(error2);
  ASSERT_FALSE(status_or.ok());
  EXPECT_EQ("error2", status_or.status().message());

  // Overwrite the error with a value.
  status_or = std::move(value2);
  ASSERT_TRUE(status_or.ok());
  EXPECT_EQ(std::string(1000, '2'), status_or.value());
}

TEST(StatusOr, TestCopyWithValuesAndErrors) {
  StatusOr<std::string> status_or(std::string(1000, '0'));
  StatusOr<std::string> value1(std::string(1000, '1'));
  StatusOr<std::string> value2(std::string(1000, '2'));
  StatusOr<std::string> error1(Status(absl::StatusCode::kUnknown, "error1"));
  StatusOr<std::string> error2(Status(absl::StatusCode::kUnknown, "error2"));

  ASSERT_TRUE(status_or.ok());
  EXPECT_EQ(std::string(1000, '0'), status_or.value());

  // Overwrite the value in status_or with another value.
  status_or = value1;
  ASSERT_TRUE(status_or.ok());
  EXPECT_EQ(std::string(1000, '1'), status_or.value());

  // Overwrite the value in status_or with an error.
  status_or = error1;
  ASSERT_FALSE(status_or.ok());
  EXPECT_EQ("error1", status_or.status().message());

  // Overwrite the error in status_or with another error.
  status_or = error2;
  ASSERT_FALSE(status_or.ok());
  EXPECT_EQ("error2", status_or.status().message());

  // Overwrite the error with a value.
  status_or = value2;
  ASSERT_TRUE(status_or.ok());
  EXPECT_EQ(std::string(1000, '2'), status_or.value());

  // Verify original values unchanged.
  EXPECT_EQ(std::string(1000, '1'), value1.value());
  EXPECT_EQ("error1", error1.status().message());
  EXPECT_EQ("error2", error2.status().message());
  EXPECT_EQ(std::string(1000, '2'), value2.value());
}

TEST(StatusOr, TestDefaultCtor) {
  StatusOr<int> thing;
  EXPECT_FALSE(thing.ok());
  EXPECT_EQ(thing.status().code(), absl::StatusCode::kUnknown);
}

TEST(StatusOrDeathTest, TestDefaultCtorValue) {
  StatusOr<int> thing;
  EXPECT_DEATH(thing.value(), "");

  const StatusOr<int> thing2;
  EXPECT_DEATH(thing.value(), "");
}

TEST(StatusOr, TestStatusCtor) {
  StatusOr<int> thing(Status(absl::StatusCode::kCancelled, ""));
  EXPECT_FALSE(thing.ok());
  EXPECT_EQ(thing.status().code(), absl::StatusCode::kCancelled);
}

TEST(StatusOr, TestValueCtor) {
  const int kI = 4;
  const StatusOr<int> thing(kI);
  EXPECT_TRUE(thing.ok());
  EXPECT_EQ(kI, thing.value());
}

TEST(StatusOr, TestCopyCtorStatusOk) {
  const int kI = 4;
  const StatusOr<int> original(kI);
  const StatusOr<int> copy(original);
  EXPECT_EQ(copy.status(), original.status());
  EXPECT_EQ(original.value(), copy.value());
}

TEST(StatusOr, TestCopyCtorStatusNotOk) {
  StatusOr<int> original(Status(absl::StatusCode::kCancelled, ""));
  StatusOr<int> copy(original);
  EXPECT_EQ(copy.status(), original.status());
}

TEST(StatusOr, TestCopyCtorNonAssignable) {
  const int kI = 4;
  CopyNoAssign value(kI);
  StatusOr<CopyNoAssign> original(value);
  StatusOr<CopyNoAssign> copy(original);
  EXPECT_EQ(copy.status(), original.status());
  EXPECT_EQ(original.value().foo_, copy.value().foo_);
}

TEST(StatusOr, TestCopyCtorStatusOKConverting) {
  const int kI = 4;
  StatusOr<int> original(kI);
  StatusOr<double> copy(original);
  EXPECT_EQ(copy.status(), original.status());
  EXPECT_DOUBLE_EQ(original.value(), copy.value());
}

TEST(StatusOr, TestCopyCtorStatusNotOkConverting) {
  StatusOr<int> original(Status(absl::StatusCode::kCancelled, ""));
  StatusOr<double> copy(original);
  EXPECT_EQ(copy.status(), original.status());
}

TEST(StatusOr, TestAssignmentStatusOk) {
  const int kI = 4;
  StatusOr<int> source(kI);
  StatusOr<int> target;
  target = source;
  EXPECT_EQ(target.status(), source.status());
  EXPECT_EQ(source.value(), target.value());
}

TEST(StatusOr, TestAssignmentStatusNotOk) {
  StatusOr<int> source(Status(absl::StatusCode::kCancelled, ""));
  StatusOr<int> target;
  target = source;
  EXPECT_EQ(target.status(), source.status());
}

TEST(StatusOr, TestStatus) {
  StatusOr<int> good(4);
  EXPECT_TRUE(good.ok());
  StatusOr<int> bad(Status(absl::StatusCode::kCancelled, ""));
  EXPECT_FALSE(bad.ok());
  EXPECT_EQ(bad.status(), Status(absl::StatusCode::kCancelled, ""));
}

TEST(StatusOr, TestValue) {
  const int kI = 4;
  StatusOr<int> thing(kI);
  EXPECT_EQ(kI, thing.value());
}

TEST(StatusOr, TestValueConst) {
  const int kI = 4;
  const StatusOr<int> thing(kI);
  EXPECT_EQ(kI, thing.value());
}

TEST(StatusOrDeathTest, TestValueNotOk) {
  StatusOr<int> thing(Status(absl::StatusCode::kCancelled, "cancelled"));
  EXPECT_DEATH(thing.value(), "");
}

TEST(StatusOrDeathTest, TestValueNotOkConst) {
  const StatusOr<int> thing(Status(absl::StatusCode::kUnknown, ""));
  EXPECT_DEATH(thing.value(), "");
}

TEST(StatusOr, TestPointerDefaultCtor) {
  StatusOr<int*> thing;
  EXPECT_FALSE(thing.ok());
  EXPECT_EQ(thing.status().code(), absl::StatusCode::kUnknown);
}

TEST(StatusOrDeathTest, TestPointerDefaultCtorValue) {
  StatusOr<int*> thing;
  EXPECT_DEATH(thing.value(), "");
}

TEST(StatusOr, TestPointerStatusCtor) {
  StatusOr<int*> thing(Status(absl::StatusCode::kCancelled, ""));
  EXPECT_FALSE(thing.ok());
  EXPECT_EQ(thing.status(), Status(absl::StatusCode::kCancelled, ""));
}

TEST(StatusOr, TestPointerValueCtor) {
  const int kI = 4;
  StatusOr<const int*> thing(&kI);
  EXPECT_TRUE(thing.ok());
  EXPECT_EQ(&kI, thing.value());
}

TEST(StatusOr, TestPointerCopyCtorStatusOk) {
  const int kI = 0;
  StatusOr<const int*> original(&kI);
  StatusOr<const int*> copy(original);
  EXPECT_EQ(copy.status(), original.status());
  EXPECT_EQ(original.value(), copy.value());
}

TEST(StatusOr, TestPointerCopyCtorStatusNotOk) {
  StatusOr<int*> original(Status(absl::StatusCode::kCancelled, ""));
  StatusOr<int*> copy(original);
  EXPECT_EQ(copy.status(), original.status());
}

TEST(StatusOr, TestPointerCopyCtorStatusOKConverting) {
  Derived derived;
  StatusOr<Derived*> original(&derived);
  StatusOr<Base2*> copy(original);
  EXPECT_EQ(copy.status(), original.status());
  EXPECT_EQ(static_cast<const Base2*>(original.value()),
            copy.value());
}

TEST(StatusOr, TestPointerCopyCtorStatusNotOkConverting) {
  StatusOr<Derived*> original(Status(absl::StatusCode::kCancelled, ""));
  StatusOr<Base2*> copy(original);
  EXPECT_EQ(copy.status(), original.status());
}

TEST(StatusOr, TestPointerAssignmentStatusOk) {
  const int kI = 0;
  StatusOr<const int*> source(&kI);
  StatusOr<const int*> target;
  target = source;
  EXPECT_EQ(target.status(), source.status());
  EXPECT_EQ(source.value(), target.value());
}

TEST(StatusOr, TestPointerAssignmentStatusNotOk) {
  StatusOr<int*> source(Status(absl::StatusCode::kCancelled, ""));
  StatusOr<int*> target;
  target = source;
  EXPECT_EQ(target.status(), source.status());
}

TEST(StatusOr, TestPointerStatus) {
  const int kI = 0;
  StatusOr<const int*> good(&kI);
  EXPECT_TRUE(good.ok());
  StatusOr<const int*> bad(Status(absl::StatusCode::kCancelled, ""));
  EXPECT_EQ(bad.status(), Status(absl::StatusCode::kCancelled, ""));
}

TEST(StatusOr, TestPointerValue) {
  const int kI = 0;
  StatusOr<const int*> thing(&kI);
  EXPECT_EQ(&kI, thing.value());
}

TEST(StatusOr, TestPointerValueConst) {
  const int kI = 0;
  const StatusOr<const int*> thing(&kI);
  EXPECT_EQ(&kI, thing.value());
}

// NOTE(tucker): StatusOr does not support this kind
// of resize op.
// TEST(StatusOr, StatusOrVectorOfUniquePointerCanResize) {
//   using EvilType = std::vector<std::unique_ptr<int>>;
//   static_assert(std::is_copy_constructible<EvilType>::value, "");
//   std::vector<StatusOr<EvilType>> v(5);
//   v.reserve(v.capacity() + 10);
// }

TEST(StatusOrDeathTest, TestPointerValueNotOk) {
  StatusOr<int*> thing(Status(absl::StatusCode::kCancelled, "cancelled"));
  EXPECT_DEATH(thing.value(), "");
}

TEST(StatusOrDeathTest, TestPointerValueNotOkConst) {
  const StatusOr<int*> thing(Status(absl::StatusCode::kCancelled, "cancelled"));
  EXPECT_DEATH(thing.value(), "");
}

// Benchmarks were removed as we not intend to change forked code.

} // namespace
} // namespace absl
