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
  FilterStateImplTest() {
    resetFilterState();
    Assert::resetEnvoyBugCountersForTest();
  }

  void resetFilterState() {
    filter_state_ = std::make_unique<FilterStateImpl>(FilterState::LifeSpan::FilterChain);
  }
  FilterState& filterState() { return *filter_state_; }

private:
  std::unique_ptr<FilterStateImpl> filter_state_;
};

} // namespace

TEST_F(FilterStateImplTest, Simple) {
  size_t access_count = 0u;
  size_t destruction_count = 0u;
  filterState().setData(
      "test_name", std::make_unique<TestStoredTypeTracking>(5, &access_count, &destruction_count),
      FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(0u, access_count);
  EXPECT_EQ(0u, destruction_count);

  EXPECT_EQ(5, filterState().getDataReadOnly<TestStoredTypeTracking>("test_name")->access());
  EXPECT_EQ(1u, access_count);
  EXPECT_EQ(0u, destruction_count);

  resetFilterState();
  EXPECT_EQ(1u, access_count);
  EXPECT_EQ(1u, destruction_count);
}

TEST_F(FilterStateImplTest, SharedPointerAccessor) {
  size_t access_count = 0u;
  size_t destruction_count = 0u;
  filterState().setData(
      "test_name", std::make_shared<TestStoredTypeTracking>(5, &access_count, &destruction_count),
      FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(0u, access_count);
  EXPECT_EQ(0u, destruction_count);

  {
    auto obj = filterState().getDataSharedMutableGeneric("test_name");
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

  filterState().setData(
      "test_1",
      std::make_unique<TestStoredTypeTracking>(ValueOne, &access_count_1, &destruction_count),
      FilterState::LifeSpan::FilterChain);
  filterState().setData(
      "test_2",
      std::make_unique<TestStoredTypeTracking>(ValueTwo, &access_count_2, &destruction_count),
      FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(0u, access_count_1);
  EXPECT_EQ(0u, access_count_2);
  EXPECT_EQ(0u, destruction_count);

  EXPECT_EQ(ValueOne, filterState().getDataReadOnly<TestStoredTypeTracking>("test_1")->access());
  EXPECT_EQ(1u, access_count_1);
  EXPECT_EQ(0u, access_count_2);
  EXPECT_EQ(ValueTwo, filterState().getDataReadOnly<TestStoredTypeTracking>("test_2")->access());
  EXPECT_EQ(1u, access_count_1);
  EXPECT_EQ(1u, access_count_2);
  resetFilterState();
  EXPECT_EQ(2u, destruction_count);
}

TEST_F(FilterStateImplTest, SimpleTypeReadOnly) {
  filterState().setData("test_1", std::make_unique<SimpleType>(1),
                        FilterState::LifeSpan::FilterChain);
  filterState().setData("test_2", std::make_unique<SimpleType>(2),
                        FilterState::LifeSpan::FilterChain);

  EXPECT_EQ(1, filterState().getDataReadOnly<SimpleType>("test_1")->access());
  EXPECT_EQ(2, filterState().getDataReadOnly<SimpleType>("test_2")->access());
}

TEST_F(FilterStateImplTest, SimpleTypeMutable) {
  filterState().setData("test_1", std::make_unique<SimpleType>(1),
                        FilterState::LifeSpan::FilterChain);
  filterState().setData("test_2", std::make_unique<SimpleType>(2),
                        FilterState::LifeSpan::FilterChain);

  EXPECT_EQ(1, filterState().getDataReadOnly<SimpleType>("test_1")->access());
  EXPECT_EQ(2, filterState().getDataReadOnly<SimpleType>("test_2")->access());

  filterState().getDataMutable<SimpleType>("test_1")->set(100);
  filterState().getDataMutable<SimpleType>("test_2")->set(200);
  EXPECT_EQ(100, filterState().getDataReadOnly<SimpleType>("test_1")->access());
  EXPECT_EQ(200, filterState().getDataReadOnly<SimpleType>("test_2")->access());
}

TEST_F(FilterStateImplTest, NoNameConflictMutableAndMutable) {
  // Mutable data can be overwritten by another mutable data of same or different type.

  // mutable + mutable - same type
  filterState().setData("test_2", std::make_unique<SimpleType>(3),
                        FilterState::LifeSpan::FilterChain);
  filterState().setData("test_2", std::make_unique<SimpleType>(4),
                        FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(4, filterState().getDataMutable<SimpleType>("test_2")->access());

  // mutable + mutable - different types
  filterState().setData("test_4", std::make_unique<SimpleType>(7),
                        FilterState::LifeSpan::FilterChain);
  filterState().setData("test_4", std::make_unique<TestStoredTypeTracking>(8, nullptr, nullptr),
                        FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(8, filterState().getDataReadOnly<TestStoredTypeTracking>("test_4")->access());
}

TEST_F(FilterStateImplTest, UnknownName) {
  EXPECT_EQ(nullptr, filterState().getDataReadOnly<SimpleType>("test_1"));
  EXPECT_EQ(nullptr, filterState().getDataMutable<SimpleType>("test_1"));
  EXPECT_EQ(nullptr, filterState().getDataSharedMutableGeneric("test_1"));
}

TEST_F(FilterStateImplTest, WrongTypeGet) {
  filterState().setData("test_name", std::make_unique<TestStoredTypeTracking>(5, nullptr, nullptr),
                        FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(5, filterState().getDataReadOnly<TestStoredTypeTracking>("test_name")->access());
  EXPECT_EQ(nullptr, filterState().getDataReadOnly<SimpleType>("test_name"));
}

namespace {

class A : public FilterState::Object {};

class B : public A {};

class C : public B {};

} // namespace

TEST_F(FilterStateImplTest, FungibleInheritance) {
  filterState().setData("testB", std::make_unique<B>(), FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasData<B>("testB"));
  EXPECT_TRUE(filterState().hasData<A>("testB"));
  EXPECT_FALSE(filterState().hasData<C>("testB"));

  filterState().setData("testC", std::make_unique<C>(), FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasData<B>("testC"));
  EXPECT_TRUE(filterState().hasData<A>("testC"));
  EXPECT_TRUE(filterState().hasData<C>("testC"));
}

TEST_F(FilterStateImplTest, HasData) {
  filterState().setData("test_1", std::make_unique<SimpleType>(1),
                        FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasData<SimpleType>("test_1"));
  EXPECT_FALSE(filterState().hasData<SimpleType>("test_2"));
  EXPECT_FALSE(filterState().hasData<TestStoredTypeTracking>("test_1"));
  EXPECT_FALSE(filterState().hasData<TestStoredTypeTracking>("test_2"));
  EXPECT_TRUE(filterState().hasDataWithName("test_1"));
  EXPECT_FALSE(filterState().hasDataWithName("test_2"));
}

TEST_F(FilterStateImplTest, LifeSpanInitFromParent) {
  filterState().setData("test_1", std::make_unique<SimpleType>(1),
                        FilterState::LifeSpan::FilterChain);
  filterState().setData("test_2", std::make_unique<SimpleType>(2),
                        FilterState::LifeSpan::FilterChain);
  filterState().setData("test_3", std::make_unique<SimpleType>(3), FilterState::LifeSpan::Request);
  filterState().setData("test_4", std::make_unique<SimpleType>(4), FilterState::LifeSpan::Request);
  filterState().setData("test_5", std::make_unique<SimpleType>(5),
                        FilterState::LifeSpan::Connection);
  filterState().setData("test_6", std::make_unique<SimpleType>(6),
                        FilterState::LifeSpan::Connection);

  FilterStateImpl new_filter_state(filterState().parent(), FilterState::LifeSpan::FilterChain);
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_1"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_2"));
  EXPECT_TRUE(new_filter_state.hasDataWithName("test_3"));
  EXPECT_TRUE(new_filter_state.hasDataWithName("test_4"));
  EXPECT_TRUE(new_filter_state.hasDataWithName("test_5"));
  EXPECT_TRUE(new_filter_state.hasDataWithName("test_6"));
  EXPECT_EQ(3, new_filter_state.getDataMutable<SimpleType>("test_3")->access());
  EXPECT_EQ(4, new_filter_state.getDataMutable<SimpleType>("test_4")->access());
  EXPECT_EQ(5, new_filter_state.getDataMutable<SimpleType>("test_5")->access());
  EXPECT_EQ(6, new_filter_state.getDataMutable<SimpleType>("test_6")->access());
}

TEST_F(FilterStateImplTest, LifeSpanInitFromGrandparent) {
  filterState().setData("test_1", std::make_unique<SimpleType>(1),
                        FilterState::LifeSpan::FilterChain);
  filterState().setData("test_2", std::make_unique<SimpleType>(2),
                        FilterState::LifeSpan::FilterChain);
  filterState().setData("test_3", std::make_unique<SimpleType>(3), FilterState::LifeSpan::Request);
  filterState().setData("test_4", std::make_unique<SimpleType>(4), FilterState::LifeSpan::Request);
  filterState().setData("test_5", std::make_unique<SimpleType>(5),
                        FilterState::LifeSpan::Connection);
  filterState().setData("test_6", std::make_unique<SimpleType>(6),
                        FilterState::LifeSpan::Connection);

  FilterStateImpl new_filter_state(filterState().parent()->parent(),
                                   FilterState::LifeSpan::FilterChain);
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_1"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_2"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_3"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_4"));
  EXPECT_TRUE(new_filter_state.hasDataWithName("test_5"));
  EXPECT_TRUE(new_filter_state.hasDataWithName("test_6"));
  EXPECT_EQ(5, new_filter_state.getDataMutable<SimpleType>("test_5")->access());
  EXPECT_EQ(6, new_filter_state.getDataMutable<SimpleType>("test_6")->access());
}

TEST_F(FilterStateImplTest, LifeSpanInitFromNonParent) {
  filterState().setData("test_1", std::make_unique<SimpleType>(1),
                        FilterState::LifeSpan::FilterChain);
  filterState().setData("test_2", std::make_unique<SimpleType>(2),
                        FilterState::LifeSpan::FilterChain);
  filterState().setData("test_3", std::make_unique<SimpleType>(3), FilterState::LifeSpan::Request);
  filterState().setData("test_4", std::make_unique<SimpleType>(4), FilterState::LifeSpan::Request);
  filterState().setData("test_5", std::make_unique<SimpleType>(5),
                        FilterState::LifeSpan::Connection);
  filterState().setData("test_6", std::make_unique<SimpleType>(6),
                        FilterState::LifeSpan::Connection);

  FilterStateImpl new_filter_state(filterState().parent(), FilterState::LifeSpan::Request);
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_1"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_2"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_3"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_4"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_5"));
  EXPECT_FALSE(new_filter_state.hasDataWithName("test_6"));
}

TEST_F(FilterStateImplTest, SharedWithUpstream) {
  auto shared = std::make_shared<SimpleType>(1);
  filterState().setData("shared_1", shared, FilterState::LifeSpan::FilterChain,
                        StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  filterState().setData("test_2", std::make_shared<SimpleType>(2),
                        FilterState::LifeSpan::FilterChain);
  filterState().setData("test_3", std::make_shared<SimpleType>(3), FilterState::LifeSpan::Request);
  filterState().setData("shared_4", std::make_shared<SimpleType>(4), FilterState::LifeSpan::Request,
                        StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  filterState().setData("shared_5", std::make_shared<SimpleType>(5),
                        FilterState::LifeSpan::Connection,
                        StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  filterState().setData("test_6", std::make_shared<SimpleType>(6),
                        FilterState::LifeSpan::Connection);
  filterState().setData("shared_7", std::make_shared<SimpleType>(7),
                        FilterState::LifeSpan::Connection,
                        StreamSharingMayImpactPooling::SharedWithUpstreamConnectionOnce);
  auto objects = filterState().objectsSharedWithUpstreamConnection();
  EXPECT_EQ(objects->size(), 4);
  std::sort(objects->begin(), objects->end(),
            [](const auto& lhs, const auto& rhs) -> bool { return lhs.name_ < rhs.name_; });
  EXPECT_EQ(objects->at(0).name_, "shared_1");
  EXPECT_EQ(objects->at(0).stream_sharing_,
            StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  EXPECT_EQ(objects->at(0).data_.get(), shared.get());
  EXPECT_EQ(objects->at(1).name_, "shared_4");
  EXPECT_EQ(objects->at(1).stream_sharing_,
            StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  EXPECT_EQ(objects->at(2).name_, "shared_5");
  EXPECT_EQ(objects->at(2).stream_sharing_,
            StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  EXPECT_EQ(objects->at(3).name_, "shared_7");
  EXPECT_EQ(objects->at(3).stream_sharing_, StreamSharingMayImpactPooling::None);
}

TEST_F(FilterStateImplTest, HasDataAtOrAboveLifeSpan) {
  filterState().setData("test_1", std::make_unique<SimpleType>(1),
                        FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::FilterChain));
  EXPECT_FALSE(filterState().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Request));
  EXPECT_FALSE(filterState().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));

  filterState().setData("test_2", std::make_unique<SimpleType>(2), FilterState::LifeSpan::Request);
  EXPECT_TRUE(filterState().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::FilterChain));
  EXPECT_TRUE(filterState().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Request));
  EXPECT_FALSE(filterState().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));

  filterState().setData("test_3", std::make_unique<SimpleType>(3),
                        FilterState::LifeSpan::Connection);
  EXPECT_TRUE(filterState().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::FilterChain));
  EXPECT_TRUE(filterState().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Request));
  EXPECT_TRUE(filterState().hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));
}

TEST_F(FilterStateImplTest, SetSameDataWithDifferentLifeSpan) {
  filterState().setData("test_1", std::make_unique<SimpleType>(1),
                        FilterState::LifeSpan::Connection);
  // Test reset on smaller LifeSpan
  EXPECT_ENVOY_BUG(filterState().setData("test_1", std::make_unique<SimpleType>(2),
                                         FilterState::LifeSpan::FilterChain),
                   "FilterStateAccessViolation: FilterState::setData<T> called twice with "
                   "conflicting life_span on the same data_name: test_1.");
  Assert::resetEnvoyBugCountersForTest();
  EXPECT_ENVOY_BUG(filterState().setData("test_1", std::make_unique<SimpleType>(2),
                                         FilterState::LifeSpan::Request),
                   "FilterStateAccessViolation: FilterState::setData<T> called twice with "
                   "conflicting life_span on the same data_name: test_1.");

  // Still mutable on the correct LifeSpan.
  filterState().setData("test_1", std::make_unique<SimpleType>(2),
                        FilterState::LifeSpan::Connection);
  EXPECT_EQ(2, filterState().getDataMutable<SimpleType>("test_1")->access());

  filterState().setData("test_2", std::make_unique<SimpleType>(1), FilterState::LifeSpan::Request);
  // Test reset on smaller and greater LifeSpan
  EXPECT_ENVOY_BUG(filterState().setData("test_2", std::make_unique<SimpleType>(2),
                                         FilterState::LifeSpan::FilterChain),
                   "FilterStateAccessViolation: FilterState::setData<T> called twice with "
                   "conflicting life_span on the same data_name: test_2.");
  Assert::resetEnvoyBugCountersForTest();
  EXPECT_ENVOY_BUG(filterState().setData("test_2", std::make_unique<SimpleType>(2),
                                         FilterState::LifeSpan::Connection),
                   "FilterStateAccessViolation: FilterState::setData<T> called twice with "
                   "conflicting life_span on the same data_name: test_2.");

  // Still mutable on the correct LifeSpan.
  filterState().setData("test_2", std::make_unique<SimpleType>(2), FilterState::LifeSpan::Request);
  EXPECT_EQ(2, filterState().getDataMutable<SimpleType>("test_2")->access());
}

TEST_F(FilterStateImplTest, IndexedFilterStateBasicCrud) {
  const std::string name = std::string(FilterState::indexToName(FilterStateIndex::LocalReplyOwner));

  // Test basic set/get by index
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, "my_indexed_key",
                               std::make_shared<SimpleType>(42),
                               FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasIndexedData(FilterStateIndex::LocalReplyOwner));
  EXPECT_EQ(42, filterState()
                    .getIndexedDataReadOnly<SimpleType>(FilterStateIndex::LocalReplyOwner)
                    ->access());
  EXPECT_EQ(
      42,
      filterState().getIndexedDataMutable<SimpleType>(FilterStateIndex::LocalReplyOwner)->access());

  // Test set by name (legacy) and retrieve by index (transparent optimization)
  resetFilterState();
  filterState().setData(name, std::make_shared<SimpleType>(100),
                        FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasIndexedData(FilterStateIndex::LocalReplyOwner));
  EXPECT_EQ(100, filterState()
                     .getIndexedDataReadOnly<SimpleType>(FilterStateIndex::LocalReplyOwner)
                     ->access());

  // Test set by index and retrieve by name (transparent optimization)
  resetFilterState();
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                               std::make_shared<SimpleType>(200),
                               FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasData<SimpleType>(name));
  EXPECT_EQ(200, filterState().getDataReadOnly<SimpleType>(name)->access());

  // Legacy Name-Based Mutable Retrieval of Indexed Data
  resetFilterState();
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                               std::make_shared<SimpleType>(500),
                               FilterState::LifeSpan::FilterChain);
  auto* obj_mutable = filterState().getDataMutable<SimpleType>(name);
  ASSERT_NE(nullptr, obj_mutable);
  EXPECT_EQ(500, obj_mutable->access());

  // hasDataWithName legacy check with an indexed key name
  resetFilterState();
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                               std::make_shared<SimpleType>(42),
                               FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasDataWithName(name));
}

TEST_F(FilterStateImplTest, IndexedFilterStateParentPropagation) {
  const std::string name = std::string(FilterState::indexToName(FilterStateIndex::LocalReplyOwner));

  // Test parent propagation of indexed data (Read-only)
  auto parent_state = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  parent_state->setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                               std::make_shared<SimpleType>(200),
                               FilterState::LifeSpan::Connection);
  auto child_state =
      std::make_shared<FilterStateImpl>(parent_state, FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(child_state->hasIndexedData(FilterStateIndex::LocalReplyOwner));
  EXPECT_EQ(
      200,
      child_state->getIndexedDataReadOnly<SimpleType>(FilterStateIndex::LocalReplyOwner)->access());

  // Test parent propagation of indexed data (Mutable/Shared)
  auto parent_shared = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  parent_shared->setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                                std::make_shared<SimpleType>(600),
                                FilterState::LifeSpan::Connection);
  FilterStateImpl child_shared(parent_shared, FilterState::LifeSpan::FilterChain);
  auto* child_mutable =
      child_shared.getIndexedDataMutable<SimpleType>(FilterStateIndex::LocalReplyOwner);
  ASSERT_NE(nullptr, child_mutable);
  EXPECT_EQ(600, child_mutable->access());

  // hasDataAtOrAboveLifeSpan parent traversal
  auto parent_state_2 = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  FilterStateImpl child_state_2(parent_state_2, FilterState::LifeSpan::FilterChain);
  EXPECT_FALSE(child_state_2.hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));
  parent_state_2->setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                                 std::make_shared<SimpleType>(1),
                                 FilterState::LifeSpan::Connection);
  EXPECT_TRUE(child_state_2.hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));
}

TEST_F(FilterStateImplTest, IndexedFilterStateUpstreamSharing) {
  const std::string name = std::string(FilterState::indexToName(FilterStateIndex::LocalReplyOwner));

  // Test objectsSharedWithUpstreamConnection (SharedWithUpstreamConnection)
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                               std::make_shared<SimpleType>(300),
                               FilterState::LifeSpan::FilterChain,
                               StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  auto shared_objects = filterState().objectsSharedWithUpstreamConnection();
  ASSERT_EQ(1u, shared_objects->size());
  EXPECT_EQ(name, shared_objects->at(0).name_);

  // Test objectsSharedWithUpstreamConnection (SharedWithUpstreamConnectionOnce)
  resetFilterState();
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                               std::make_shared<SimpleType>(300),
                               FilterState::LifeSpan::FilterChain,
                               StreamSharingMayImpactPooling::SharedWithUpstreamConnectionOnce);
  shared_objects = filterState().objectsSharedWithUpstreamConnection();
  ASSERT_EQ(1u, shared_objects->size());
  EXPECT_EQ(name, shared_objects->at(0).name_);

  // Test objectsSharedWithUpstreamConnection (None)
  resetFilterState();
  filterState().setIndexedData(
      FilterStateIndex::LocalReplyOwner, name, std::make_shared<SimpleType>(300),
      FilterState::LifeSpan::FilterChain, StreamSharingMayImpactPooling::None);
  shared_objects = filterState().objectsSharedWithUpstreamConnection();
  EXPECT_TRUE(shared_objects->empty());
}

TEST_F(FilterStateImplTest, IndexedFilterStateDoubleSetConflict) {
  const std::string name = std::string(FilterState::indexToName(FilterStateIndex::LocalReplyOwner));

  // 1. Conflict: parent/longer lifespan set first, then child/shorter lifespan set second
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                               std::make_shared<SimpleType>(1), FilterState::LifeSpan::Connection);
  EXPECT_ENVOY_BUG(filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                                                std::make_shared<SimpleType>(2),
                                                FilterState::LifeSpan::FilterChain),
                   "FilterStateAccessViolation: FilterState::setIndexedData<T> called twice");

  // 2. Conflict: with a real parent instance
  auto parent = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  parent->setIndexedData(FilterStateIndex::LocalReplyOwner, name, std::make_shared<SimpleType>(1),
                         FilterState::LifeSpan::Connection);
  FilterStateImpl child(parent, FilterState::LifeSpan::FilterChain);
  EXPECT_ENVOY_BUG(child.setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                                        std::make_shared<SimpleType>(2),
                                        FilterState::LifeSpan::FilterChain),
                   "FilterStateAccessViolation: FilterState::setIndexedData<T> called twice");

  // 3. Conflict: child/shorter lifespan set first, then parent/longer lifespan set second
  resetFilterState();
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                               std::make_shared<SimpleType>(1), FilterState::LifeSpan::FilterChain);
  EXPECT_ENVOY_BUG(filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                                                std::make_shared<SimpleType>(2),
                                                FilterState::LifeSpan::Connection),
                   "FilterStateAccessViolation: FilterState::setIndexedData<T> called twice");
}

TEST_F(FilterStateImplTest, IndexedFilterStateLazyAllocation) {
  // Verifies the lazy allocation string map edge cases and boundary checks
  // No parent, no map initialized
  {
    FilterStateImpl fresh_state(FilterState::LifeSpan::FilterChain);
    EXPECT_EQ(nullptr, fresh_state.getDataReadOnlyGeneric("some_custom_key"));
    EXPECT_EQ(nullptr, fresh_state.getDataSharedMutableGeneric("some_custom_key"));
  }

  // With parent, no map initialized on child
  auto parent = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  FilterStateImpl child(parent, FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(nullptr, child.getDataReadOnlyGeneric("some_custom_key"));

  // Parent has the custom key
  parent->setData("some_custom_key", std::make_shared<SimpleType>(123),
                  FilterState::LifeSpan::Connection);
  auto* obj = child.getDataReadOnly<SimpleType>("some_custom_key");
  ASSERT_NE(nullptr, obj);
  EXPECT_EQ(123, obj->access());

  // Map is initialized on child, parent is not null, query non-existent key
  child.setData("child_only_key", std::make_shared<SimpleType>(999),
                FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(nullptr, child.getDataReadOnlyGeneric("non_existent_key"));
  EXPECT_EQ(nullptr, child.getDataSharedMutableGeneric("non_existent_key"));
}

TEST_F(FilterStateImplTest, IndexedFilterStateUtilities) {
  // Verify invalid index handling (out of bounds)
  EXPECT_FALSE(filterState().hasIndexedData(FilterStateIndex::MaxIndex));
  EXPECT_EQ(nullptr, filterState().getIndexedDataReadOnlyGeneric(FilterStateIndex::MaxIndex));
  EXPECT_EQ(nullptr, filterState().getIndexedDataSharedMutableGeneric(FilterStateIndex::MaxIndex));

  // setIndexedData with invalid index should return immediately with no-op
  filterState().setIndexedData(FilterStateIndex::MaxIndex, "invalid",
                               std::make_shared<SimpleType>(500),
                               FilterState::LifeSpan::FilterChain);

  // indexToName boundary checks
  EXPECT_EQ("", FilterState::indexToName(FilterStateIndex::MaxIndex));
  EXPECT_EQ("", FilterState::indexToName(static_cast<FilterStateIndex>(99)));

  // nameToIndex direct checks for invalid keys
  EXPECT_FALSE(FilterState::nameToIndex("non_existent_key").has_value());
}

} // namespace StreamInfo
} // namespace Envoy
