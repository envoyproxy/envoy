#include "envoy/common/exception.h"

#include "source/common/stream_info/filter_state_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace StreamInfo {
namespace {

class TestStoredTypeTracking : public FilterState::Object {
public:
  TestStoredTypeTracking(int value, size_t* access_count, size_t* destruction_count)
      : value_(value), access_count_(access_count), destruction_count_(destruction_count) {}
  ~TestStoredTypeTracking() override {
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
  void set(int value) { value_ = value; }

private:
  int value_;
};

class FilterStateImplTest : public testing::Test {
public:
  FilterStateImplTest() { resetFilterState(); }

  void resetFilterState() {
    filter_state_ = std::make_unique<FilterStateImpl>(FilterState::LifeSpan::FilterChain);
  }
  FilterState& filter_state() { return *filter_state_; }

private:
  std::unique_ptr<FilterStateImpl> filter_state_;
};

} // namespace

TEST_F(FilterStateImplTest, Simple) {
  size_t access_count = 0u;
  size_t destruction_count = 0u;
  filter_state().setData(
      "test_name", std::make_unique<TestStoredTypeTracking>(5, &access_count, &destruction_count),
      FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(0u, access_count);
  EXPECT_EQ(0u, destruction_count);

  EXPECT_EQ(5, filter_state().getDataReadOnly<TestStoredTypeTracking>("test_name")->access());
  EXPECT_EQ(1u, access_count);
  EXPECT_EQ(0u, destruction_count);

  resetFilterState();
  EXPECT_EQ(1u, access_count);
  EXPECT_EQ(1u, destruction_count);
}

TEST_F(FilterStateImplTest, SharedPointerAccessor) {
  size_t access_count = 0u;
  size_t destruction_count = 0u;
  filter_state().setData(
      "test_name", std::make_shared<TestStoredTypeTracking>(5, &access_count, &destruction_count),
      FilterState::StateType::Mutable, FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(0u, access_count);
  EXPECT_EQ(0u, destruction_count);

  {
    auto obj = filter_state().getDataSharedMutableGeneric("test_name");
    EXPECT_EQ(5, dynamic_cast<TestStoredTypeTracking*>(obj.get())->access());
    EXPECT_EQ(1u, access_count);
    EXPECT_EQ(0u, destruction_count);

    resetFilterState();
    EXPECT_EQ(1u, access_count);
    EXPECT_EQ(0u, destruction_count);
  }

  EXPECT_EQ(1u, destruction_count);
}

TEST_F(FilterStateImplTest, SameTypes) {
  size_t access_count_1 = 0u;
  size_t access_count_2 = 0u;
  size_t destruction_count = 0u;
  static const int ValueOne = 5;
  static const int ValueTwo = 6;

  filter_state().setData(
      "test_1",
      std::make_unique<TestStoredTypeTracking>(ValueOne, &access_count_1, &destruction_count),
      FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  filter_state().setData(
      "test_2",
      std::make_unique<TestStoredTypeTracking>(ValueTwo, &access_count_2, &destruction_count),
      FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(0u, access_count_1);
  EXPECT_EQ(0u, access_count_2);
  EXPECT_EQ(0u, destruction_count);

  EXPECT_EQ(ValueOne, filter_state().getDataReadOnly<TestStoredTypeTracking>("test_1")->access());
  EXPECT_EQ(1u, access_count_1);
  EXPECT_EQ(0u, access_count_2);
  EXPECT_EQ(ValueTwo, filter_state().getDataReadOnly<TestStoredTypeTracking>("test_2")->access());
  EXPECT_EQ(1u, access_count_1);
  EXPECT_EQ(1u, access_count_2);
  resetFilterState();
  EXPECT_EQ(2u, destruction_count);
}

TEST_F(FilterStateImplTest, SimpleTypeReadOnly) {
  filter_state().setData("test_1", std::make_unique<SimpleType>(1),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  filter_state().setData("test_2", std::make_unique<SimpleType>(2),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);

  EXPECT_EQ(1, filter_state().getDataReadOnly<SimpleType>("test_1")->access());
  EXPECT_EQ(2, filter_state().getDataReadOnly<SimpleType>("test_2")->access());
}

TEST_F(FilterStateImplTest, SimpleTypeMutable) {
  filter_state().setData("test_1", std::make_unique<SimpleType>(1), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::FilterChain);
  filter_state().setData("test_2", std::make_unique<SimpleType>(2), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::FilterChain);

  EXPECT_EQ(1, filter_state().getDataReadOnly<SimpleType>("test_1")->access());
  EXPECT_EQ(2, filter_state().getDataReadOnly<SimpleType>("test_2")->access());

  filter_state().getDataMutable<SimpleType>("test_1")->set(100);
  filter_state().getDataMutable<SimpleType>("test_2")->set(200);
  EXPECT_EQ(100, filter_state().getDataReadOnly<SimpleType>("test_1")->access());
  EXPECT_EQ(200, filter_state().getDataReadOnly<SimpleType>("test_2")->access());
}

TEST_F(FilterStateImplTest, NameConflictReadOnly) {
  // read only data cannot be overwritten (by any state type)
  filter_state().setData("test_1", std::make_unique<SimpleType>(1),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  EXPECT_THROW_WITH_MESSAGE(
      filter_state().setData("test_1", std::make_unique<SimpleType>(2),
                             FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain),
      EnvoyException, "FilterState::setData<T> called twice on same ReadOnly state.");
  EXPECT_THROW_WITH_MESSAGE(
      filter_state().setData("test_1", std::make_unique<SimpleType>(2),
                             FilterState::StateType::Mutable, FilterState::LifeSpan::FilterChain),
      EnvoyException, "FilterState::setData<T> called twice on same ReadOnly state.");
  EXPECT_EQ(1, filter_state().getDataReadOnly<SimpleType>("test_1")->access());
}

TEST_F(FilterStateImplTest, NameConflictDifferentTypesReadOnly) {
  filter_state().setData("test_1", std::make_unique<SimpleType>(1),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  EXPECT_THROW_WITH_MESSAGE(
      filter_state().setData("test_1",
                             std::make_unique<TestStoredTypeTracking>(2, nullptr, nullptr),
                             FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain),
      EnvoyException, "FilterState::setData<T> called twice on same ReadOnly state.");
}

TEST_F(FilterStateImplTest, NameConflictMutableAndReadOnly) {
  // Mutable data cannot be overwritten by read only data.
  filter_state().setData("test_1", std::make_unique<SimpleType>(1), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::FilterChain);
  EXPECT_THROW_WITH_MESSAGE(
      filter_state().setData("test_1", std::make_unique<SimpleType>(2),
                             FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain),
      EnvoyException, "FilterState::setData<T> called twice with different state types.");
}

TEST_F(FilterStateImplTest, NoNameConflictMutableAndMutable) {
  // Mutable data can be overwritten by another mutable data of same or different type.

  // mutable + mutable - same type
  filter_state().setData("test_2", std::make_unique<SimpleType>(3), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::FilterChain);
  filter_state().setData("test_2", std::make_unique<SimpleType>(4), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(4, filter_state().getDataMutable<SimpleType>("test_2")->access());

  // mutable + mutable - different types
  filter_state().setData("test_4", std::make_unique<SimpleType>(7), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::FilterChain);
  filter_state().setData("test_4", std::make_unique<TestStoredTypeTracking>(8, nullptr, nullptr),
                         FilterState::StateType::Mutable, FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(8, filter_state().getDataReadOnly<TestStoredTypeTracking>("test_4")->access());
}

TEST_F(FilterStateImplTest, UnknownName) {
  EXPECT_EQ(nullptr, filter_state().getDataReadOnly<SimpleType>("test_1"));
  EXPECT_EQ(nullptr, filter_state().getDataMutable<SimpleType>("test_1"));
  EXPECT_EQ(nullptr, filter_state().getDataSharedMutableGeneric("test_1"));
}

TEST_F(FilterStateImplTest, WrongTypeGet) {
  filter_state().setData("test_name", std::make_unique<TestStoredTypeTracking>(5, nullptr, nullptr),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(5, filter_state().getDataReadOnly<TestStoredTypeTracking>("test_name")->access());
  EXPECT_EQ(nullptr, filter_state().getDataReadOnly<SimpleType>("test_name"));
}

TEST_F(FilterStateImplTest, ErrorAccessingReadOnlyAsMutable) {
  // Accessing read only data as mutable should throw error
  filter_state().setData("test_name", std::make_unique<TestStoredTypeTracking>(5, nullptr, nullptr),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  EXPECT_THROW_WITH_MESSAGE(filter_state().getDataMutable<TestStoredTypeTracking>("test_name"),
                            EnvoyException,
                            "FilterState tried to access immutable data as mutable.");
  EXPECT_THROW_WITH_MESSAGE(filter_state().getDataSharedMutableGeneric("test_name"), EnvoyException,
                            "FilterState tried to access immutable data as mutable.");
}

namespace {

class A : public FilterState::Object {};

class B : public A {};

class C : public B {};

} // namespace

TEST_F(FilterStateImplTest, FungibleInheritance) {
  filter_state().setData("testB", std::make_unique<B>(), FilterState::StateType::ReadOnly,
                         FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_state().hasData<B>("testB"));
  EXPECT_TRUE(filter_state().hasData<A>("testB"));
  EXPECT_FALSE(filter_state().hasData<C>("testB"));

  filter_state().setData("testC", std::make_unique<C>(), FilterState::StateType::ReadOnly,
                         FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_state().hasData<B>("testC"));
  EXPECT_TRUE(filter_state().hasData<A>("testC"));
  EXPECT_TRUE(filter_state().hasData<C>("testC"));
}

TEST_F(FilterStateImplTest, HasData) {
  filter_state().setData("test_1", std::make_unique<SimpleType>(1),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_state().hasData<SimpleType>("test_1"));
  EXPECT_FALSE(filter_state().hasData<SimpleType>("test_2"));
  EXPECT_FALSE(filter_state().hasData<TestStoredTypeTracking>("test_1"));
  EXPECT_FALSE(filter_state().hasData<TestStoredTypeTracking>("test_2"));
  EXPECT_TRUE(filter_state().hasDataWithName("test_1"));
  EXPECT_FALSE(filter_state().hasDataWithName("test_2"));
}

TEST_F(FilterStateImplTest, LifeSpanInitFromParent) {
  filter_state().setData("test_1", std::make_unique<SimpleType>(1),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  filter_state().setData("test_2", std::make_unique<SimpleType>(2), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::FilterChain);
  filter_state().setData("test_3", std::make_unique<SimpleType>(3),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::Request);
  filter_state().setData("test_4", std::make_unique<SimpleType>(4), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::Request);
  filter_state().setData("test_5", std::make_unique<SimpleType>(5),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::Connection);
  filter_state().setData("test_6", std::make_unique<SimpleType>(6), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::Connection);

  FilterStateImpl new_filter_state(filter_state().parent(), FilterState::LifeSpan::FilterChain);
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_1"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_2"));
  EXPECT_TRUE(new_filter_state.hasDataWithName("test_3"));
  EXPECT_TRUE(new_filter_state.hasDataWithName("test_4"));
  EXPECT_TRUE(new_filter_state.hasDataWithName("test_5"));
  EXPECT_TRUE(new_filter_state.hasDataWithName("test_6"));
  EXPECT_THROW_WITH_MESSAGE(new_filter_state.getDataMutable<SimpleType>("test_3"), EnvoyException,
                            "FilterState tried to access immutable data as mutable.");

  EXPECT_EQ(4, new_filter_state.getDataMutable<SimpleType>("test_4")->access());

  EXPECT_THROW_WITH_MESSAGE(new_filter_state.getDataMutable<SimpleType>("test_5"), EnvoyException,
                            "FilterState tried to access immutable data as mutable.");

  EXPECT_EQ(6, new_filter_state.getDataMutable<SimpleType>("test_6")->access());
}

TEST_F(FilterStateImplTest, LifeSpanInitFromGrandparent) {
  filter_state().setData("test_1", std::make_unique<SimpleType>(1),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  filter_state().setData("test_2", std::make_unique<SimpleType>(2), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::FilterChain);
  filter_state().setData("test_3", std::make_unique<SimpleType>(3),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::Request);
  filter_state().setData("test_4", std::make_unique<SimpleType>(4), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::Request);
  filter_state().setData("test_5", std::make_unique<SimpleType>(5),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::Connection);
  filter_state().setData("test_6", std::make_unique<SimpleType>(6), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::Connection);

  FilterStateImpl new_filter_state(filter_state().parent()->parent(),
                                   FilterState::LifeSpan::FilterChain);
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_1"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_2"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_3"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_4"));
  EXPECT_TRUE(new_filter_state.hasDataWithName("test_5"));
  EXPECT_TRUE(new_filter_state.hasDataWithName("test_6"));
  EXPECT_THROW_WITH_MESSAGE(new_filter_state.getDataMutable<SimpleType>("test_5"), EnvoyException,
                            "FilterState tried to access immutable data as mutable.");
  EXPECT_EQ(6, new_filter_state.getDataMutable<SimpleType>("test_6")->access());
}

TEST_F(FilterStateImplTest, LifeSpanInitFromNonParent) {
  filter_state().setData("test_1", std::make_unique<SimpleType>(1),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  filter_state().setData("test_2", std::make_unique<SimpleType>(2), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::FilterChain);
  filter_state().setData("test_3", std::make_unique<SimpleType>(3),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::Request);
  filter_state().setData("test_4", std::make_unique<SimpleType>(4), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::Request);
  filter_state().setData("test_5", std::make_unique<SimpleType>(5),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::Connection);
  filter_state().setData("test_6", std::make_unique<SimpleType>(6), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::Connection);

  FilterStateImpl new_filter_state(filter_state().parent(), FilterState::LifeSpan::Request);
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_1"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_2"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_3"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_4"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_5"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_6"));
}

TEST_F(FilterStateImplTest, SharedWithUpstream) {
  auto shared = std::make_shared<SimpleType>(1);
  filter_state().setData("shared_1", shared, FilterState::StateType::ReadOnly,
                         FilterState::LifeSpan::FilterChain,
                         FilterState::StreamSharing::SharedWithUpstreamConnection);
  filter_state().setData("test_2", std::make_shared<SimpleType>(2), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::FilterChain);
  filter_state().setData("test_3", std::make_shared<SimpleType>(3),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::Request);
  filter_state().setData("shared_4", std::make_shared<SimpleType>(4),
                         FilterState::StateType::Mutable, FilterState::LifeSpan::Request,
                         FilterState::StreamSharing::SharedWithUpstreamConnection);
  filter_state().setData("shared_5", std::make_shared<SimpleType>(5),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::Connection,
                         FilterState::StreamSharing::SharedWithUpstreamConnection);
  filter_state().setData("test_6", std::make_shared<SimpleType>(6), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::Connection);
  auto objects = filter_state().objectsSharedWithUpstreamConnection();
  EXPECT_EQ(objects->size(), 3);
  EXPECT_EQ(objects->at(0).name_, "shared_5");
  EXPECT_EQ(objects->at(0).state_type_, FilterState::StateType::ReadOnly);
  EXPECT_EQ(objects->at(1).name_, "shared_4");
  EXPECT_EQ(objects->at(1).state_type_, FilterState::StateType::Mutable);
  EXPECT_EQ(objects->at(2).name_, "shared_1");
  EXPECT_EQ(objects->at(2).state_type_, FilterState::StateType::ReadOnly);
  EXPECT_EQ(objects->at(2).data_.get(), shared.get());
}

TEST_F(FilterStateImplTest, HasDataAtOrAboveLifeSpan) {
  filter_state().setData("test_1", std::make_unique<SimpleType>(1),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_state().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::FilterChain));
  EXPECT_FALSE(filter_state().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Request));
  EXPECT_FALSE(filter_state().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));

  filter_state().setData("test_2", std::make_unique<SimpleType>(2),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::Request);
  EXPECT_TRUE(filter_state().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::FilterChain));
  EXPECT_TRUE(filter_state().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Request));
  EXPECT_FALSE(filter_state().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));

  filter_state().setData("test_3", std::make_unique<SimpleType>(3),
                         FilterState::StateType::ReadOnly, FilterState::LifeSpan::Connection);
  EXPECT_TRUE(filter_state().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::FilterChain));
  EXPECT_TRUE(filter_state().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Request));
  EXPECT_TRUE(filter_state().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));
}

TEST_F(FilterStateImplTest, SetSameDataWithDifferentLifeSpan) {
  filter_state().setData("test_1", std::make_unique<SimpleType>(1), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::Connection);
  // Test reset on smaller LifeSpan
  EXPECT_THROW_WITH_MESSAGE(
      filter_state().setData("test_1", std::make_unique<SimpleType>(2),
                             FilterState::StateType::Mutable, FilterState::LifeSpan::FilterChain),
      EnvoyException,
      "FilterState::setData<T> called twice with conflicting life_span on the same data_name.");
  EXPECT_THROW_WITH_MESSAGE(
      filter_state().setData("test_1", std::make_unique<SimpleType>(2),
                             FilterState::StateType::Mutable, FilterState::LifeSpan::Request),
      EnvoyException,
      "FilterState::setData<T> called twice with conflicting life_span on the same data_name.");

  // Still mutable on the correct LifeSpan.
  filter_state().setData("test_1", std::make_unique<SimpleType>(2), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::Connection);
  EXPECT_EQ(2, filter_state().getDataMutable<SimpleType>("test_1")->access());

  filter_state().setData("test_2", std::make_unique<SimpleType>(1), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::Request);
  // Test reset on smaller and greater LifeSpan
  EXPECT_THROW_WITH_MESSAGE(
      filter_state().setData("test_2", std::make_unique<SimpleType>(2),
                             FilterState::StateType::Mutable, FilterState::LifeSpan::FilterChain),
      EnvoyException,
      "FilterState::setData<T> called twice with conflicting life_span on the same data_name.");
  EXPECT_THROW_WITH_MESSAGE(
      filter_state().setData("test_2", std::make_unique<SimpleType>(2),
                             FilterState::StateType::Mutable, FilterState::LifeSpan::Connection),
      EnvoyException,
      "FilterState::setData<T> called twice with conflicting life_span on the same data_name.");

  // Still mutable on the correct LifeSpan.
  filter_state().setData("test_2", std::make_unique<SimpleType>(2), FilterState::StateType::Mutable,
                         FilterState::LifeSpan::Request);
  EXPECT_EQ(2, filter_state().getDataMutable<SimpleType>("test_2")->access());
}

} // namespace StreamInfo
} // namespace Envoy
