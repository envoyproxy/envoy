#include "envoy/common/exception.h"

#include "common/request_info/filter_state_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace RequestInfo {
namespace {

class TestStoredTypeTracking : public FilterState::Object {
public:
  TestStoredTypeTracking(int value, size_t* access_count, size_t* destruction_count)
      : value_(value), access_count_(access_count), destruction_count_(destruction_count) {}
  ~TestStoredTypeTracking() {
    if (destruction_count_) {
      ++*destruction_count_;
    }
  }

  int access() const {
    if (access_count_) {
      ++*access_count_;
    }
    return value_;
  }

private:
  int value_;
  size_t* access_count_;
  size_t* destruction_count_;
};

class SimpleType : public FilterState::Object {
public:
  SimpleType(int value) : value_(value) {}

  int access() const { return value_; }

private:
  int value_;
};

class FilterStateImplTest : public testing::Test {
public:
  FilterStateImplTest() { resetDynamicMetadata(); }

  void resetDynamicMetadata() { dynamic_metadata_ = std::make_unique<FilterStateImpl>(); }
  FilterState& dynamic_metadata() { return *dynamic_metadata_; }

private:
  std::unique_ptr<FilterStateImpl> dynamic_metadata_;
};

} // namespace

TEST_F(FilterStateImplTest, Simple) {
  size_t access_count = 0u;
  size_t destruction_count = 0u;
  dynamic_metadata().setData(
      "test_name", std::make_unique<TestStoredTypeTracking>(5, &access_count, &destruction_count));
  EXPECT_EQ(0u, access_count);
  EXPECT_EQ(0u, destruction_count);

  EXPECT_EQ(5, dynamic_metadata().getData<TestStoredTypeTracking>("test_name").access());
  EXPECT_EQ(1u, access_count);
  EXPECT_EQ(0u, destruction_count);

  resetDynamicMetadata();
  EXPECT_EQ(1u, access_count);
  EXPECT_EQ(1u, destruction_count);
}

TEST_F(FilterStateImplTest, SameTypes) {
  size_t access_count_1 = 0u;
  size_t access_count_2 = 0u;
  size_t destruction_count = 0u;
  static const int ValueOne = 5;
  static const int ValueTwo = 6;

  dynamic_metadata().setData("test_1", std::make_unique<TestStoredTypeTracking>(
                                           ValueOne, &access_count_1, &destruction_count));
  dynamic_metadata().setData("test_2", std::make_unique<TestStoredTypeTracking>(
                                           ValueTwo, &access_count_2, &destruction_count));
  EXPECT_EQ(0u, access_count_1);
  EXPECT_EQ(0u, access_count_2);
  EXPECT_EQ(0u, destruction_count);

  EXPECT_EQ(ValueOne, dynamic_metadata().getData<TestStoredTypeTracking>("test_1").access());
  EXPECT_EQ(1u, access_count_1);
  EXPECT_EQ(0u, access_count_2);
  EXPECT_EQ(ValueTwo, dynamic_metadata().getData<TestStoredTypeTracking>("test_2").access());
  EXPECT_EQ(1u, access_count_1);
  EXPECT_EQ(1u, access_count_2);
  resetDynamicMetadata();
  EXPECT_EQ(2u, destruction_count);
}

TEST_F(FilterStateImplTest, SimpleType) {
  dynamic_metadata().setData("test_1", std::make_unique<SimpleType>(1));
  dynamic_metadata().setData("test_2", std::make_unique<SimpleType>(2));

  EXPECT_EQ(1, dynamic_metadata().getData<SimpleType>("test_1").access());
  EXPECT_EQ(2, dynamic_metadata().getData<SimpleType>("test_2").access());
}

TEST_F(FilterStateImplTest, NameConflict) {
  dynamic_metadata().setData("test_1", std::make_unique<SimpleType>(1));
  EXPECT_THROW_WITH_MESSAGE(dynamic_metadata().setData("test_1", std::make_unique<SimpleType>(2)),
                            EnvoyException, "FilterState::setData<T> called twice with same name.");
  EXPECT_EQ(1, dynamic_metadata().getData<SimpleType>("test_1").access());
}

TEST_F(FilterStateImplTest, NameConflictDifferentTypes) {
  dynamic_metadata().setData("test_1", std::make_unique<SimpleType>(1));
  EXPECT_THROW_WITH_MESSAGE(
      dynamic_metadata().setData("test_1",
                                 std::make_unique<TestStoredTypeTracking>(2, nullptr, nullptr)),
      EnvoyException, "FilterState::setData<T> called twice with same name.");
}

TEST_F(FilterStateImplTest, UnknownName) {
  EXPECT_THROW_WITH_MESSAGE(dynamic_metadata().getData<SimpleType>("test_1"), EnvoyException,
                            "FilterState::getData<T> called for unknown data name.");
}

TEST_F(FilterStateImplTest, WrongTypeGet) {
  dynamic_metadata().setData("test_name",
                             std::make_unique<TestStoredTypeTracking>(5, nullptr, nullptr));
  EXPECT_EQ(5, dynamic_metadata().getData<TestStoredTypeTracking>("test_name").access());
  EXPECT_THROW_WITH_MESSAGE(dynamic_metadata().getData<SimpleType>("test_name"), EnvoyException,
                            "Data stored under test_name cannot be coerced to specified type");
}

namespace {

class A : public FilterState::Object {};

class B : public A {};

class C : public B {};

} // namespace

TEST_F(FilterStateImplTest, FungibleInheritance) {
  dynamic_metadata().setData("testB", std::make_unique<B>());
  EXPECT_TRUE(dynamic_metadata().hasData<B>("testB"));
  EXPECT_TRUE(dynamic_metadata().hasData<A>("testB"));
  EXPECT_FALSE(dynamic_metadata().hasData<C>("testB"));

  dynamic_metadata().setData("testC", std::make_unique<C>());
  EXPECT_TRUE(dynamic_metadata().hasData<B>("testC"));
  EXPECT_TRUE(dynamic_metadata().hasData<A>("testC"));
  EXPECT_TRUE(dynamic_metadata().hasData<C>("testC"));
}

TEST_F(FilterStateImplTest, HasData) {
  dynamic_metadata().setData("test_1", std::make_unique<SimpleType>(1));
  EXPECT_TRUE(dynamic_metadata().hasData<SimpleType>("test_1"));
  EXPECT_FALSE(dynamic_metadata().hasData<SimpleType>("test_2"));
  EXPECT_FALSE(dynamic_metadata().hasData<TestStoredTypeTracking>("test_1"));
  EXPECT_FALSE(dynamic_metadata().hasData<TestStoredTypeTracking>("test_2"));
  EXPECT_TRUE(dynamic_metadata().hasDataWithName("test_1"));
  EXPECT_FALSE(dynamic_metadata().hasDataWithName("test_2"));
}

} // namespace RequestInfo
} // namespace Envoy
