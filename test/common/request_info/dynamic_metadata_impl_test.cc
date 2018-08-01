#include "envoy/common/exception.h"

#include "common/request_info/dynamic_metadata_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace RequestInfo {
namespace {

class TestStoredTypeTracking : public DynamicMetadata::DynamicMetadataObject {
 public:
  TestStoredTypeTracking(int value, size_t* access_count, size_t* destruction_count)
      : value_(value), access_count_(access_count), destruction_count_(destruction_count) {}
  ~TestStoredTypeTracking() {
    if (destruction_count_) {
      ++*destruction_count_;
    }
  }

  int Access() const {
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

class SimpleType : public DynamicMetadata::DynamicMetadataObject {
 public:
  SimpleType(int value) : value_(value) {}

  int Access() const { return value_; }

 private:
  int value_;
};

class DynamicMetadataImplTest : public testing::Test {
public:
  DynamicMetadataImplTest() { ResetDynamicMetadata(); }

  void ResetDynamicMetadata() { dynamic_metadata_ = std::make_unique<DynamicMetadataImpl>(); }
  DynamicMetadata& dynamic_metadata() { return *dynamic_metadata_; }

private:
  std::unique_ptr<DynamicMetadataImpl> dynamic_metadata_;
};

} // namespace

TEST_F(DynamicMetadataImplTest, Simple) {
  size_t access_count = 0u;
  size_t destruction_count = 0u;
  dynamic_metadata().setData(
      "test_name", std::make_unique<TestStoredTypeTracking>(5, &access_count, &destruction_count));
  EXPECT_EQ(0u, access_count);
  EXPECT_EQ(0u, destruction_count);

  EXPECT_EQ(5, dynamic_metadata().getData<TestStoredTypeTracking>("test_name").Access());
  EXPECT_EQ(1u, access_count);
  EXPECT_EQ(0u, destruction_count);

  ResetDynamicMetadata();
  EXPECT_EQ(1u, access_count);
  EXPECT_EQ(1u, destruction_count);
}

TEST_F(DynamicMetadataImplTest, SameTypes) {
  size_t access_count_1 = 0u;
  size_t access_count_2 = 0u;
  size_t destruction_count = 0u;
  const int ValueOne = 5;
  const int ValueTwo = 6;

  dynamic_metadata().setData(
      "test_1", std::make_unique<TestStoredTypeTracking>(ValueOne, &access_count_1, &destruction_count));
  dynamic_metadata().setData(
      "test_2", std::make_unique<TestStoredTypeTracking>(ValueTwo, &access_count_2, &destruction_count));
  EXPECT_EQ(0u, access_count_1);
  EXPECT_EQ(0u, access_count_2);
  EXPECT_EQ(0u, destruction_count);

  EXPECT_EQ(ValueOne, dynamic_metadata().getData<TestStoredTypeTracking>("test_1").Access());
  EXPECT_EQ(1u, access_count_1);
  EXPECT_EQ(0u, access_count_2);
  EXPECT_EQ(ValueTwo, dynamic_metadata().getData<TestStoredTypeTracking>("test_2").Access());
  EXPECT_EQ(1u, access_count_1);
  EXPECT_EQ(1u, access_count_2);
  ResetDynamicMetadata();
  EXPECT_EQ(2u, destruction_count);
}

TEST_F(DynamicMetadataImplTest, SimpleType) {
  dynamic_metadata().setData("test_1", std::make_unique<SimpleType>(1));
  dynamic_metadata().setData("test_2", std::make_unique<SimpleType>(2));

  EXPECT_EQ(1, dynamic_metadata().getData<SimpleType>("test_1").Access());
  EXPECT_EQ(2, dynamic_metadata().getData<SimpleType>("test_2").Access());
}

TEST_F(DynamicMetadataImplTest, NameConflict) {
  dynamic_metadata().setData("test_1", std::make_unique<SimpleType>(1));
  EXPECT_THROW(dynamic_metadata().setData("test_1", std::make_unique<SimpleType>(2)), EnvoyException);
  EXPECT_EQ(1, dynamic_metadata().getData<SimpleType>("test_1").Access());
}

TEST_F(DynamicMetadataImplTest, NameConflictDifferentTypes) {
  dynamic_metadata().setData("test_1", std::make_unique<SimpleType>(1));
  EXPECT_THROW(
      dynamic_metadata().setData("test_1", std::make_unique<TestStoredTypeTracking>(2, nullptr, nullptr)),
      EnvoyException);
}

TEST_F(DynamicMetadataImplTest, UnknownName) {
  EXPECT_THROW(dynamic_metadata().getData<SimpleType>("test_1"), EnvoyException);
}

TEST_F(DynamicMetadataImplTest, WrongTypeGet) {
  dynamic_metadata().setData("test_name", std::make_unique<TestStoredTypeTracking>(5, nullptr, nullptr));
  EXPECT_EQ(5, dynamic_metadata().getData<TestStoredTypeTracking>("test_name").Access());
  EXPECT_THROW(dynamic_metadata().getData<SimpleType>("test_name"), EnvoyException);
}

namespace {

class A : public DynamicMetadata::DynamicMetadataObject {
};

class B: public A {};

class C : public B {};

} // namespace

TEST_F(DynamicMetadataImplTest, FungibleInheritance) {
  dynamic_metadata().setData("testB", std::make_unique<B>());
  EXPECT_TRUE(dynamic_metadata().hasData<B>("testB"));
  EXPECT_TRUE(dynamic_metadata().hasData<A>("testB"));
  EXPECT_FALSE(dynamic_metadata().hasData<C>("testB"));

  dynamic_metadata().setData("testC", std::make_unique<C>());
  EXPECT_TRUE(dynamic_metadata().hasData<B>("testC"));
  EXPECT_TRUE(dynamic_metadata().hasData<A>("testC"));
  EXPECT_TRUE(dynamic_metadata().hasData<C>("testC"));
}

TEST_F(DynamicMetadataImplTest, HasData) {
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
