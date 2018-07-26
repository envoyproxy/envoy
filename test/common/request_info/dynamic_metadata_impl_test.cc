#include "envoy/common/exception.h"

#include "common/request_info/dynamic_metadata_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace RequestInfo {
namespace {

class TestStoredType {
public:
  TestStoredType(int value, size_t* access_count, size_t* destruction_count)
      : value_(value), access_count_(access_count), destruction_count_(destruction_count) {}
  ~TestStoredType() {
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
      "test_name", std::make_unique<TestStoredType>(5, &access_count, &destruction_count));
  EXPECT_EQ(0u, access_count);
  EXPECT_EQ(0u, destruction_count);

  EXPECT_EQ(5, dynamic_metadata().getData<TestStoredType>("test_name").Access());
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
      "test_1", std::make_unique<TestStoredType>(ValueOne, &access_count_1, &destruction_count));
  dynamic_metadata().setData(
      "test_2", std::make_unique<TestStoredType>(ValueTwo, &access_count_2, &destruction_count));
  EXPECT_EQ(0u, access_count_1);
  EXPECT_EQ(0u, access_count_2);
  EXPECT_EQ(0u, destruction_count);

  EXPECT_EQ(ValueOne, dynamic_metadata().getData<TestStoredType>("test_1").Access());
  EXPECT_EQ(1u, access_count_1);
  EXPECT_EQ(0u, access_count_2);
  EXPECT_EQ(ValueTwo, dynamic_metadata().getData<TestStoredType>("test_2").Access());
  EXPECT_EQ(1u, access_count_1);
  EXPECT_EQ(1u, access_count_2);
  ResetDynamicMetadata();
  EXPECT_EQ(2u, destruction_count);
}

TEST_F(DynamicMetadataImplTest, SimpleType) {
  dynamic_metadata().setData("test_1", std::make_unique<int>(1));
  dynamic_metadata().setData("test_2", std::make_unique<int>(2));

  EXPECT_EQ(1, dynamic_metadata().getData<int>("test_1"));
  EXPECT_EQ(2, dynamic_metadata().getData<int>("test_2"));
}

TEST_F(DynamicMetadataImplTest, NameConflict) {
  dynamic_metadata().setData("test_1", std::make_unique<int>(1));
  EXPECT_THROW(dynamic_metadata().setData("test_1", std::make_unique<int>(2)), EnvoyException);
  EXPECT_EQ(1, dynamic_metadata().getData<int>("test_1"));
}

TEST_F(DynamicMetadataImplTest, NameConflictDifferentTypes) {
  dynamic_metadata().setData("test_1", std::make_unique<int>(1));
  EXPECT_THROW(
      dynamic_metadata().setData("test_1", std::make_unique<TestStoredType>(2, nullptr, nullptr)),
      EnvoyException);
}

TEST_F(DynamicMetadataImplTest, UnknownName) {
  EXPECT_THROW(dynamic_metadata().getData<int>("test_1"), EnvoyException);
}

TEST_F(DynamicMetadataImplTest, WrongTypeGet) {
  dynamic_metadata().setData("test_name", std::make_unique<TestStoredType>(5, nullptr, nullptr));
  EXPECT_EQ(5, dynamic_metadata().getData<TestStoredType>("test_name").Access());
  EXPECT_THROW(dynamic_metadata().getData<int>("test_name"), EnvoyException);
}

TEST_F(DynamicMetadataImplTest, HasData) {
  dynamic_metadata().setData("test_1", std::make_unique<int>(1));
  EXPECT_TRUE(dynamic_metadata().hasData<int>("test_1"));
  EXPECT_FALSE(dynamic_metadata().hasData<int>("test_2"));
  EXPECT_FALSE(dynamic_metadata().hasData<bool>("test_1"));
  EXPECT_FALSE(dynamic_metadata().hasData<bool>("test_2"));
  EXPECT_TRUE(dynamic_metadata().hasDataWithName("test_1"));
  EXPECT_FALSE(dynamic_metadata().hasDataWithName("test_2"));
}

} // namespace RequestInfo
} // namespace Envoy
