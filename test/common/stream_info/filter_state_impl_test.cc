#include <type_traits>

#include "envoy/common/exception.h"

#include "source/common/http/local_reply_owner_object.h"
#include "source/common/network/downstream_network_namespace.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/network/upstream_socket_options_filter_state.h"
#include "source/common/network/upstream_subject_alt_names.h"
#include "source/common/router/original_connect_port.h"
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

class A : public FilterState::Object {};
class B : public A {};
class C : public B {};

template <FilterStateIndex Index, typename T>
concept CanSetIndexedData =
    requires(FilterState& fs, std::shared_ptr<T> obj) { fs.setIndexedData<Index>(std::move(obj)); };

// Verify exact canonical type bindings for all default-enabled FilterStateIndex values.
static_assert(std::is_same_v<IndexedFilterStateType<FilterStateIndex::LocalReplyOwner>,
                             Http::LocalReplyOwnerObject>);
static_assert(std::is_same_v<IndexedFilterStateType<FilterStateIndex::UpstreamServerName>,
                             Network::UpstreamServerName>);
static_assert(std::is_same_v<IndexedFilterStateType<FilterStateIndex::UpstreamSocketOptions>,
                             Network::UpstreamSocketOptionsFilterState>);
static_assert(std::is_same_v<IndexedFilterStateType<FilterStateIndex::UpstreamSubjectAltNames>,
                             Network::UpstreamSubjectAltNames>);
static_assert(std::is_same_v<IndexedFilterStateType<FilterStateIndex::NetworkNamespace>,
                             Network::DownstreamNetworkNamespace>);
static_assert(std::is_same_v<IndexedFilterStateType<FilterStateIndex::OriginalConnectPort>,
                             Router::OriginalConnectPort>);

// Verify setIndexedData<Index> accepts only the canonical type (or subclasses) and rejects
// FilterState::Object or unrelated subtypes at compile time.
static_assert(CanSetIndexedData<FilterStateIndex::LocalReplyOwner, Http::LocalReplyOwnerObject>);
static_assert(!CanSetIndexedData<FilterStateIndex::LocalReplyOwner, FilterState::Object>);
static_assert(!CanSetIndexedData<FilterStateIndex::LocalReplyOwner, SimpleType>);
static_assert(!CanSetIndexedData<FilterStateIndex::LocalReplyOwner, Network::UpstreamServerName>);

static_assert(CanSetIndexedData<FilterStateIndex::UpstreamServerName, Network::UpstreamServerName>);
static_assert(!CanSetIndexedData<FilterStateIndex::UpstreamServerName, FilterState::Object>);
static_assert(!CanSetIndexedData<FilterStateIndex::UpstreamServerName, SimpleType>);
static_assert(
    !CanSetIndexedData<FilterStateIndex::UpstreamServerName, Network::UpstreamSubjectAltNames>);

static_assert(CanSetIndexedData<FilterStateIndex::UpstreamSocketOptions,
                                Network::UpstreamSocketOptionsFilterState>);
static_assert(!CanSetIndexedData<FilterStateIndex::UpstreamSocketOptions, FilterState::Object>);
static_assert(!CanSetIndexedData<FilterStateIndex::UpstreamSocketOptions, SimpleType>);

static_assert(
    CanSetIndexedData<FilterStateIndex::UpstreamSubjectAltNames, Network::UpstreamSubjectAltNames>);
static_assert(!CanSetIndexedData<FilterStateIndex::UpstreamSubjectAltNames, FilterState::Object>);
static_assert(!CanSetIndexedData<FilterStateIndex::UpstreamSubjectAltNames, SimpleType>);

static_assert(
    CanSetIndexedData<FilterStateIndex::NetworkNamespace, Network::DownstreamNetworkNamespace>);
static_assert(!CanSetIndexedData<FilterStateIndex::NetworkNamespace, FilterState::Object>);
static_assert(!CanSetIndexedData<FilterStateIndex::NetworkNamespace, SimpleType>);

static_assert(
    CanSetIndexedData<FilterStateIndex::OriginalConnectPort, Router::OriginalConnectPort>);
static_assert(!CanSetIndexedData<FilterStateIndex::OriginalConnectPort, FilterState::Object>);
static_assert(!CanSetIndexedData<FilterStateIndex::OriginalConnectPort, SimpleType>);

template <FilterStateIndex Index> void expectMismatchedStringWriteRejected(FilterState& state) {
  const std::string name = std::string(FilterState::indexToName(Index));
  state.setData(name, std::make_shared<SimpleType>(91), FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(state.hasIndexedData(Index));
  EXPECT_EQ(nullptr, state.getIndexedDataReadOnly<Index>());
  EXPECT_EQ(nullptr, state.getIndexedDataMutable<Index>());
  ASSERT_NE(nullptr, state.getDataReadOnly<SimpleType>(name));
  EXPECT_EQ(91, state.getDataReadOnly<SimpleType>(name)->access());
}

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
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(42),
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
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(200),
                               FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasData<SimpleType>(name));
  EXPECT_EQ(200, filterState().getDataReadOnly<SimpleType>(name)->access());

  // Legacy Name-Based Mutable Retrieval of Indexed Data
  resetFilterState();
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(500),
                               FilterState::LifeSpan::FilterChain);
  auto* obj_mutable = filterState().getDataMutable<SimpleType>(name);
  ASSERT_NE(nullptr, obj_mutable);
  EXPECT_EQ(500, obj_mutable->access());

  // hasDataWithName legacy check with an indexed key name
  resetFilterState();
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(42),
                               FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasDataWithName(name));
}

TEST_F(FilterStateImplTest, IndexedFilterStateParentPropagation) {
  const std::string name = std::string(FilterState::indexToName(FilterStateIndex::LocalReplyOwner));

  // Test parent propagation of indexed data (Read-only)
  auto parent_state = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  parent_state->setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(200),
                               FilterState::LifeSpan::Connection);
  auto child_state =
      std::make_shared<FilterStateImpl>(parent_state, FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(child_state->hasIndexedData(FilterStateIndex::LocalReplyOwner));
  EXPECT_EQ(
      200,
      child_state->getIndexedDataReadOnly<SimpleType>(FilterStateIndex::LocalReplyOwner)->access());

  // Test parent propagation of indexed data (Mutable/Shared)
  auto parent_shared = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  parent_shared->setIndexedData(FilterStateIndex::LocalReplyOwner,
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
  parent_state_2->setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(1),
                                 FilterState::LifeSpan::Connection);
  EXPECT_TRUE(child_state_2.hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));
}

TEST_F(FilterStateImplTest, IndexedFilterStateUpstreamSharing) {
  const std::string name = std::string(FilterState::indexToName(FilterStateIndex::LocalReplyOwner));

  // Test objectsSharedWithUpstreamConnection (SharedWithUpstreamConnection)
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(300),
                               FilterState::LifeSpan::FilterChain,
                               StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  auto shared_objects = filterState().objectsSharedWithUpstreamConnection();
  ASSERT_EQ(1u, shared_objects->size());
  EXPECT_EQ(name, shared_objects->at(0).name_);

  // Test objectsSharedWithUpstreamConnection (SharedWithUpstreamConnectionOnce)
  resetFilterState();
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(300),
                               FilterState::LifeSpan::FilterChain,
                               StreamSharingMayImpactPooling::SharedWithUpstreamConnectionOnce);
  shared_objects = filterState().objectsSharedWithUpstreamConnection();
  ASSERT_EQ(1u, shared_objects->size());
  EXPECT_EQ(name, shared_objects->at(0).name_);

  // Test objectsSharedWithUpstreamConnection (None)
  resetFilterState();
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(300),
                               FilterState::LifeSpan::FilterChain,
                               StreamSharingMayImpactPooling::None);
  shared_objects = filterState().objectsSharedWithUpstreamConnection();
  EXPECT_TRUE(shared_objects->empty());
}

TEST_F(FilterStateImplTest, IndexedFilterStateDoubleSetConflict) {
  // 1. Conflict: parent/longer lifespan set first, then child/shorter lifespan set second
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(1),
                               FilterState::LifeSpan::Connection);
  EXPECT_ENVOY_BUG(filterState().setIndexedData(FilterStateIndex::LocalReplyOwner,
                                                std::make_shared<SimpleType>(2),
                                                FilterState::LifeSpan::FilterChain),
                   "FilterStateAccessViolation: FilterState::setIndexedData<T> called twice");

  // 2. Conflict: with a real parent instance
  auto parent = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  parent->setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(1),
                         FilterState::LifeSpan::Connection);
  FilterStateImpl child(parent, FilterState::LifeSpan::FilterChain);
  EXPECT_ENVOY_BUG(child.setIndexedData(FilterStateIndex::LocalReplyOwner,
                                        std::make_shared<SimpleType>(2),
                                        FilterState::LifeSpan::FilterChain),
                   "FilterStateAccessViolation: FilterState::setIndexedData<T> called twice");

  // 3. Conflict: child/shorter lifespan set first, then parent/longer lifespan set second
  resetFilterState();
  filterState().setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(1),
                               FilterState::LifeSpan::FilterChain);
  EXPECT_ENVOY_BUG(filterState().setIndexedData(FilterStateIndex::LocalReplyOwner,
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
  const FilterStateIndex invalid_index = static_cast<FilterStateIndex>(FilterStateIndexCount);

  // Verify invalid index handling (out of bounds)
  EXPECT_FALSE(filterState().hasIndexedData(invalid_index));
  EXPECT_EQ(nullptr, filterState().getIndexedDataReadOnlyGeneric(invalid_index));
  EXPECT_EQ(nullptr, filterState().getIndexedDataSharedMutableGeneric(invalid_index));

  // setIndexedData with invalid index should return immediately with no-op
  filterState().setIndexedData(invalid_index, std::make_shared<SimpleType>(500),
                               FilterState::LifeSpan::FilterChain);

  // indexToName boundary checks
  EXPECT_EQ("", FilterState::indexToName(invalid_index));
  EXPECT_EQ("", FilterState::indexToName(static_cast<FilterStateIndex>(99)));

  // nameToIndex direct checks for invalid keys
  EXPECT_FALSE(FilterState::nameToIndex("non_existent_key").has_value());

  // nameToIndex and indexToName mapping checks for NetworkNamespace
  EXPECT_EQ(FilterStateIndex::NetworkNamespace,
            FilterState::nameToIndex("envoy.network.network_namespace"));
  EXPECT_EQ("envoy.network.network_namespace",
            FilterState::indexToName(FilterStateIndex::NetworkNamespace));
}

TEST_F(FilterStateImplTest, TypedIndexedFilterStateBasicCrud) {
  const std::string name =
      std::string(FilterState::indexToName(FilterStateIndex::OriginalConnectPort));

  EXPECT_EQ(nullptr, filterState().getIndexedDataReadOnly<FilterStateIndex::OriginalConnectPort>());
  EXPECT_EQ(nullptr, filterState().getIndexedDataMutable<FilterStateIndex::OriginalConnectPort>());
  EXPECT_EQ(nullptr, filterState().getIndexedDataReadOnly<Router::OriginalConnectPort>(
                         FilterStateIndex::OriginalConnectPort));
  EXPECT_EQ(nullptr, filterState().getIndexedDataMutable<Router::OriginalConnectPort>(
                         FilterStateIndex::OriginalConnectPort));

  // Nullptr setIndexedData is a no-op for both compile-time and legacy overloads.
  filterState().setIndexedData<FilterStateIndex::OriginalConnectPort>(
      nullptr, FilterState::LifeSpan::FilterChain);
  filterState().setIndexedData(FilterStateIndex::OriginalConnectPort, nullptr,
                               FilterState::LifeSpan::FilterChain);
  EXPECT_FALSE(filterState().hasIndexedData(FilterStateIndex::OriginalConnectPort));

  // Test basic set/get by compile-time index.
  filterState().setIndexedData<FilterStateIndex::OriginalConnectPort>(
      std::make_shared<Router::OriginalConnectPort>(42), FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasIndexedData(FilterStateIndex::OriginalConnectPort));
  EXPECT_EQ(42u,
            filterState().getIndexedDataReadOnly<FilterStateIndex::OriginalConnectPort>()->value());
  EXPECT_EQ(42u,
            filterState().getIndexedDataMutable<FilterStateIndex::OriginalConnectPort>()->value());

  // Test set by name (legacy) with matching bound type and retrieve by compile-time index.
  resetFilterState();
  filterState().setData(name, std::make_shared<Router::OriginalConnectPort>(100),
                        FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasIndexedData(FilterStateIndex::OriginalConnectPort));
  EXPECT_EQ(100u,
            filterState().getIndexedDataReadOnly<FilterStateIndex::OriginalConnectPort>()->value());

  // Test set by compile-time index and retrieve by name (transparent optimization).
  resetFilterState();
  filterState().setIndexedData<FilterStateIndex::OriginalConnectPort>(
      std::make_shared<Router::OriginalConnectPort>(200), FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasData<Router::OriginalConnectPort>(name));
  EXPECT_EQ(200u, filterState().getDataReadOnly<Router::OriginalConnectPort>(name)->value());

  // Legacy name-based mutable retrieval of typed indexed data.
  resetFilterState();
  filterState().setIndexedData<FilterStateIndex::OriginalConnectPort>(
      std::make_shared<Router::OriginalConnectPort>(500), FilterState::LifeSpan::FilterChain);
  auto* obj_mutable = filterState().getDataMutable<Router::OriginalConnectPort>(name);
  ASSERT_NE(nullptr, obj_mutable);
  EXPECT_EQ(500u, obj_mutable->value());

  // hasDataWithName legacy check with a typed indexed key.
  resetFilterState();
  filterState().setIndexedData<FilterStateIndex::OriginalConnectPort>(
      std::make_shared<Router::OriginalConnectPort>(42), FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasDataWithName(name));

  // Backwards-compatible runtime-indexed overloads route canonical types to indexed storage and
  // non-canonical types to map storage without corrupting compile-time typed getters.
  resetFilterState();
  filterState().setIndexedData(FilterStateIndex::OriginalConnectPort,
                               std::make_shared<Router::OriginalConnectPort>(77),
                               FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(77u,
            filterState().getIndexedDataReadOnly<FilterStateIndex::OriginalConnectPort>()->value());
  EXPECT_EQ(77u, filterState()
                     .getIndexedDataReadOnly<Router::OriginalConnectPort>(
                         FilterStateIndex::OriginalConnectPort)
                     ->value());
  EXPECT_EQ(77u, filterState()
                     .getIndexedDataMutable<Router::OriginalConnectPort>(
                         FilterStateIndex::OriginalConnectPort)
                     ->value());
  EXPECT_EQ(nullptr, filterState().getIndexedDataReadOnly<SimpleType>(
                         FilterStateIndex::OriginalConnectPort));
  EXPECT_EQ(nullptr,
            filterState().getIndexedDataMutable<SimpleType>(FilterStateIndex::OriginalConnectPort));

  // Same-lifespan overwrite from canonical to non-canonical via legacy setIndexedData clears the
  // indexed slot while remaining readable through legacy getters.
  filterState().setIndexedData(FilterStateIndex::OriginalConnectPort,
                               std::make_shared<SimpleType>(99),
                               FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasIndexedData(FilterStateIndex::OriginalConnectPort));
  EXPECT_EQ(nullptr, filterState().getIndexedDataReadOnly<FilterStateIndex::OriginalConnectPort>());
  EXPECT_EQ(nullptr, filterState().getIndexedDataReadOnly<Router::OriginalConnectPort>(
                         FilterStateIndex::OriginalConnectPort));
  ASSERT_NE(nullptr, filterState().getIndexedDataReadOnly<SimpleType>(
                         FilterStateIndex::OriginalConnectPort));
  EXPECT_EQ(99, filterState()
                    .getIndexedDataReadOnly<SimpleType>(FilterStateIndex::OriginalConnectPort)
                    ->access());
}

TEST_F(FilterStateImplTest, IndexedFilterStateMismatchedStringWriteSafety) {
  const std::string name =
      std::string(FilterState::indexToName(FilterStateIndex::UpstreamServerName));

  // Writing an unrelated Object subtype via string setData must NOT populate indexed_data_storage_,
  // preventing static_cast type confusion on getIndexedDataReadOnly / getIndexedDataMutable, while
  // hasIndexedData still reports key occupancy consistently with hasDataWithName.
  filterState().setData(name, std::make_shared<SimpleType>(77), FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasIndexedData(FilterStateIndex::UpstreamServerName));
  EXPECT_EQ(nullptr, filterState().getIndexedDataReadOnly<FilterStateIndex::UpstreamServerName>());
  EXPECT_EQ(nullptr, filterState().getIndexedDataMutable<FilterStateIndex::UpstreamServerName>());

  // Name-based lookup still sees the stored SimpleType object.
  EXPECT_TRUE(filterState().hasDataWithName(name));
  ASSERT_NE(nullptr, filterState().getDataReadOnly<SimpleType>(name));
  EXPECT_EQ(77, filterState().getDataReadOnly<SimpleType>(name)->access());
  EXPECT_EQ(77, filterState().getDataMutable<SimpleType>(name)->access());

  // Same-lifespan overwrite with the canonical indexed type migrates cleanly to indexed storage.
  filterState().setIndexedData<FilterStateIndex::UpstreamServerName>(
      std::make_shared<Network::UpstreamServerName>("example.com"),
      FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasIndexedData(FilterStateIndex::UpstreamServerName));
  EXPECT_EQ("example.com",
            filterState().getIndexedDataReadOnly<FilterStateIndex::UpstreamServerName>()->value());
  EXPECT_EQ(nullptr, filterState().getDataReadOnly<SimpleType>(name));

  // Same-lifespan overwrite back to an unrelated type clears the indexed slot cleanly while
  // keeping hasIndexedData consistent with key occupancy.
  filterState().setData(name, std::make_shared<SimpleType>(88), FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filterState().hasIndexedData(FilterStateIndex::UpstreamServerName));
  EXPECT_EQ(nullptr, filterState().getIndexedDataReadOnly<FilterStateIndex::UpstreamServerName>());
  EXPECT_EQ(88, filterState().getDataReadOnly<SimpleType>(name)->access());

  // A non-canonical object held at a longer lifespan must read as present to hasIndexedData() on
  // shorter-lifespan children so guards like router auto_sni / auto_san_validation do not attempt
  // a conflicting FilterChain write and trigger ENVOY_BUG.
  auto parent = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  FilterStateImpl child(parent, FilterState::LifeSpan::FilterChain);
  // Three-level miss (FilterChain -> Request -> Connection) on well-known keys returns nullptr.
  EXPECT_EQ(nullptr, child.getDataReadOnlyGeneric(name));
  EXPECT_EQ(nullptr, child.getDataSharedMutableGeneric(name));

  parent->setData(name, std::make_shared<SimpleType>(55), FilterState::LifeSpan::Connection);
  parent->setData(FilterState::indexToName(FilterStateIndex::UpstreamSubjectAltNames),
                  std::make_shared<SimpleType>(56), FilterState::LifeSpan::Connection);
  EXPECT_TRUE(child.hasIndexedData(FilterStateIndex::UpstreamServerName));
  EXPECT_TRUE(child.hasIndexedData(FilterStateIndex::UpstreamSubjectAltNames));
  EXPECT_EQ(nullptr, child.getIndexedDataReadOnly<FilterStateIndex::UpstreamServerName>());
  EXPECT_EQ(nullptr, child.getIndexedDataReadOnly<FilterStateIndex::UpstreamSubjectAltNames>());
  EXPECT_EQ(55, child.getDataReadOnly<SimpleType>(name)->access());
  EXPECT_EQ(55, child.getDataMutable<SimpleType>(name)->access());
  EXPECT_EQ(56, child
                    .getDataReadOnly<SimpleType>(
                        FilterState::indexToName(FilterStateIndex::UpstreamSubjectAltNames))
                    ->access());
  EXPECT_EQ(56, child
                    .getDataMutable<SimpleType>(
                        FilterState::indexToName(FilterStateIndex::UpstreamSubjectAltNames))
                    ->access());

  // Verify runtime mismatched-type string writes are kept out of indexed storage for every index.
  resetFilterState();
  expectMismatchedStringWriteRejected<FilterStateIndex::LocalReplyOwner>(filterState());
  expectMismatchedStringWriteRejected<FilterStateIndex::UpstreamServerName>(filterState());
  expectMismatchedStringWriteRejected<FilterStateIndex::UpstreamSocketOptions>(filterState());
  expectMismatchedStringWriteRejected<FilterStateIndex::UpstreamSubjectAltNames>(filterState());
  expectMismatchedStringWriteRejected<FilterStateIndex::NetworkNamespace>(filterState());
  expectMismatchedStringWriteRejected<FilterStateIndex::OriginalConnectPort>(filterState());
}

TEST_F(FilterStateImplTest, TypedIndexedFilterStateParentPropagation) {
  // Test parent propagation of typed indexed data (Read-only).
  auto parent_state = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  parent_state->setIndexedData<FilterStateIndex::OriginalConnectPort>(
      std::make_shared<Router::OriginalConnectPort>(200), FilterState::LifeSpan::Connection);
  auto child_state =
      std::make_shared<FilterStateImpl>(parent_state, FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(child_state->hasIndexedData(FilterStateIndex::OriginalConnectPort));
  EXPECT_EQ(200u,
            child_state->getIndexedDataReadOnly<FilterStateIndex::OriginalConnectPort>()->value());

  // Test parent propagation of typed indexed data (Mutable/Shared).
  auto parent_shared = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  parent_shared->setIndexedData<FilterStateIndex::OriginalConnectPort>(
      std::make_shared<Router::OriginalConnectPort>(600), FilterState::LifeSpan::Connection);
  FilterStateImpl child_shared(parent_shared, FilterState::LifeSpan::FilterChain);
  auto* child_mutable = child_shared.getIndexedDataMutable<FilterStateIndex::OriginalConnectPort>();
  ASSERT_NE(nullptr, child_mutable);
  EXPECT_EQ(600u, child_mutable->value());

  // hasDataAtOrAboveLifeSpan parent traversal with typed indexed data.
  auto parent_state_2 = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  FilterStateImpl child_state_2(parent_state_2, FilterState::LifeSpan::FilterChain);
  EXPECT_FALSE(child_state_2.hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));
  parent_state_2->setIndexedData<FilterStateIndex::OriginalConnectPort>(
      std::make_shared<Router::OriginalConnectPort>(1), FilterState::LifeSpan::Connection);
  EXPECT_TRUE(child_state_2.hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));
}

TEST_F(FilterStateImplTest, TypedIndexedFilterStateUpstreamSharing) {
  const std::string name = std::string(FilterState::indexToName(FilterStateIndex::LocalReplyOwner));

  filterState().setIndexedData<FilterStateIndex::LocalReplyOwner>(
      std::make_shared<Http::LocalReplyOwnerObject>("owner"), FilterState::LifeSpan::FilterChain,
      StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  auto shared_objects = filterState().objectsSharedWithUpstreamConnection();
  ASSERT_EQ(1u, shared_objects->size());
  EXPECT_EQ(name, shared_objects->at(0).name_);

  resetFilterState();
  filterState().setIndexedData<FilterStateIndex::LocalReplyOwner>(
      std::make_shared<Http::LocalReplyOwnerObject>("owner"), FilterState::LifeSpan::FilterChain,
      StreamSharingMayImpactPooling::SharedWithUpstreamConnectionOnce);
  shared_objects = filterState().objectsSharedWithUpstreamConnection();
  ASSERT_EQ(1u, shared_objects->size());
  EXPECT_EQ(name, shared_objects->at(0).name_);

  resetFilterState();
  filterState().setIndexedData<FilterStateIndex::LocalReplyOwner>(
      std::make_shared<Http::LocalReplyOwnerObject>("owner"), FilterState::LifeSpan::FilterChain,
      StreamSharingMayImpactPooling::None);
  shared_objects = filterState().objectsSharedWithUpstreamConnection();
  EXPECT_TRUE(shared_objects->empty());
}

TEST_F(FilterStateImplTest, TypedIndexedFilterStateDoubleSetConflict) {
  const std::string name =
      std::string(FilterState::indexToName(FilterStateIndex::OriginalConnectPort));

  // 1. Conflict: parent/longer lifespan set first, then child/shorter lifespan set second.
  filterState().setIndexedData<FilterStateIndex::OriginalConnectPort>(
      std::make_shared<Router::OriginalConnectPort>(1), FilterState::LifeSpan::Connection);
  EXPECT_ENVOY_BUG(
      filterState().setIndexedData<FilterStateIndex::OriginalConnectPort>(
          std::make_shared<Router::OriginalConnectPort>(2), FilterState::LifeSpan::FilterChain),
      "FilterStateAccessViolation: FilterState::setIndexedData<T> called twice");

  // 2. Conflict: with a real parent instance.
  auto parent = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  parent->setIndexedData<FilterStateIndex::OriginalConnectPort>(
      std::make_shared<Router::OriginalConnectPort>(1), FilterState::LifeSpan::Connection);
  FilterStateImpl child(parent, FilterState::LifeSpan::FilterChain);
  EXPECT_ENVOY_BUG(
      child.setIndexedData<FilterStateIndex::OriginalConnectPort>(
          std::make_shared<Router::OriginalConnectPort>(2), FilterState::LifeSpan::FilterChain),
      "FilterStateAccessViolation: FilterState::setIndexedData<T> called twice");

  // 3. Conflict: child/shorter lifespan set first, then parent/longer lifespan set second.
  resetFilterState();
  filterState().setIndexedData<FilterStateIndex::OriginalConnectPort>(
      std::make_shared<Router::OriginalConnectPort>(1), FilterState::LifeSpan::FilterChain);
  EXPECT_ENVOY_BUG(
      filterState().setIndexedData<FilterStateIndex::OriginalConnectPort>(
          std::make_shared<Router::OriginalConnectPort>(2), FilterState::LifeSpan::Connection),
      "FilterStateAccessViolation: FilterState::setIndexedData<T> called twice");

  // 4. Cross-storage conflicts between untyped setData/setIndexedData and typed setIndexedData.
  resetFilterState();
  filterState().setData(name, std::make_shared<SimpleType>(1), FilterState::LifeSpan::FilterChain);
  EXPECT_ENVOY_BUG(
      filterState().setIndexedData<FilterStateIndex::OriginalConnectPort>(
          std::make_shared<Router::OriginalConnectPort>(2), FilterState::LifeSpan::Connection),
      "FilterStateAccessViolation: FilterState::setIndexedData<T> called twice");

  resetFilterState();
  filterState().setData(name, std::make_shared<SimpleType>(1), FilterState::LifeSpan::Connection);
  EXPECT_ENVOY_BUG(
      filterState().setIndexedData<FilterStateIndex::OriginalConnectPort>(
          std::make_shared<Router::OriginalConnectPort>(2), FilterState::LifeSpan::FilterChain),
      "FilterStateAccessViolation: FilterState::setIndexedData<T> called twice");

  resetFilterState();
  filterState().setIndexedData<FilterStateIndex::OriginalConnectPort>(
      std::make_shared<Router::OriginalConnectPort>(1), FilterState::LifeSpan::FilterChain);
  EXPECT_ENVOY_BUG(filterState().setData(name, std::make_shared<SimpleType>(2),
                                         FilterState::LifeSpan::Connection),
                   "FilterStateAccessViolation: FilterState::setData<T> called twice");

  resetFilterState();
  filterState().setIndexedData<FilterStateIndex::OriginalConnectPort>(
      std::make_shared<Router::OriginalConnectPort>(1), FilterState::LifeSpan::FilterChain);
  EXPECT_ENVOY_BUG(filterState().setIndexedData(FilterStateIndex::OriginalConnectPort,
                                                std::make_shared<SimpleType>(2),
                                                FilterState::LifeSpan::Connection),
                   "FilterStateAccessViolation: FilterState::setIndexedData<T> called twice");
}

TEST_F(FilterStateImplTest, IndexedFilterStateTypeValidationUtilities) {
  const FilterStateIndex invalid_index = static_cast<FilterStateIndex>(FilterStateIndexCount);
  EXPECT_EQ(nullptr, filterState().getIndexedDataReadOnly<SimpleType>(invalid_index));
  EXPECT_EQ(nullptr, filterState().getIndexedDataMutable<SimpleType>(invalid_index));

  // isValidObjectForIndex pairwise & negative checks across all default-enabled indices.
  const SimpleType wrong_type(1);
  const Http::LocalReplyOwnerObject local_reply_owner("filter");
  const Network::UpstreamServerName upstream_server_name("example.com");
  const Network::UpstreamSocketOptionsFilterState upstream_socket_options;
  const Network::UpstreamSubjectAltNames upstream_sans({"example.com"});
  const Network::DownstreamNetworkNamespace netns("/var/run/netns/test");
  const Router::OriginalConnectPort connect_port(443);

  const std::array<FilterStateIndex, 6> indices = {
      FilterStateIndex::LocalReplyOwner,       FilterStateIndex::UpstreamServerName,
      FilterStateIndex::UpstreamSocketOptions, FilterStateIndex::UpstreamSubjectAltNames,
      FilterStateIndex::NetworkNamespace,      FilterStateIndex::OriginalConnectPort,
  };
  const std::array<const FilterState::Object*, 6> canonical_objects = {
      &local_reply_owner, &upstream_server_name, &upstream_socket_options, &upstream_sans, &netns,
      &connect_port,
  };

  EXPECT_FALSE(FilterState::isValidObjectForIndex(invalid_index, &local_reply_owner));
  for (size_t i = 0; i < indices.size(); ++i) {
    EXPECT_FALSE(FilterState::isValidObjectForIndex(indices[i], nullptr));
    EXPECT_FALSE(FilterState::isValidObjectForIndex(indices[i], &wrong_type));
    for (size_t j = 0; j < canonical_objects.size(); ++j) {
      EXPECT_EQ(i == j, FilterState::isValidObjectForIndex(indices[i], canonical_objects[j]));
    }
  }

  // Verify indexToName and nameToIndex match each canonical class's key definition.
  const std::array<std::pair<FilterStateIndex, absl::string_view>, 6> expected_keys = {{
      {FilterStateIndex::LocalReplyOwner, Http::LocalReplyFilterStateKey},
      {FilterStateIndex::UpstreamServerName, Network::UpstreamServerName::key()},
      {FilterStateIndex::UpstreamSocketOptions, Network::UpstreamSocketOptionsFilterState::key()},
      {FilterStateIndex::UpstreamSubjectAltNames, Network::UpstreamSubjectAltNames::key()},
      {FilterStateIndex::NetworkNamespace, Network::DownstreamNetworkNamespace::key()},
      {FilterStateIndex::OriginalConnectPort, Router::OriginalConnectPort::key()},
  }};
  for (const auto& [index, key] : expected_keys) {
    EXPECT_EQ(key, FilterState::indexToName(index));
    EXPECT_EQ(index, FilterState::nameToIndex(key));
  }
}

TEST_F(FilterStateImplTest, IndexedFilterStateDualObjectBaseCanonicalization) {
  class DualObjectPrefix : public FilterState::Object {
  public:
    explicit DualObjectPrefix(int prefix_value) : prefix_value_(prefix_value) {}
    int prefixValue() const { return prefix_value_; }

  private:
    int prefix_value_;
  };

  class DualObjectUpstreamServerName : public DualObjectPrefix, public Network::UpstreamServerName {
  public:
    DualObjectUpstreamServerName(int prefix_value, absl::string_view server_name, bool& destroyed)
        : DualObjectPrefix(prefix_value), Network::UpstreamServerName(server_name),
          destroyed_(destroyed) {}
    ~DualObjectUpstreamServerName() override { destroyed_ = true; }

  private:
    bool& destroyed_;
  };

  const absl::string_view key = Network::UpstreamServerName::key();
  bool destroyed = false;

  {
    auto dual = std::make_shared<DualObjectUpstreamServerName>(42, "dual.example.com", destroyed);
    const auto* expected_sni = static_cast<const Network::UpstreamServerName*>(dual.get());
    const auto* expected_prefix = static_cast<const DualObjectPrefix*>(dual.get());

    std::shared_ptr<FilterState::Object> via_other_base(dual,
                                                        static_cast<DualObjectPrefix*>(dual.get()));
    std::shared_ptr<FilterState::Object> via_canonical_base(
        dual, static_cast<Network::UpstreamServerName*>(dual.get()));
    ASSERT_NE(via_other_base.get(), via_canonical_base.get());

    // isValidObjectForIndex requires the pointer to already refer to T's Object base subobject.
    EXPECT_FALSE(FilterState::isValidObjectForIndex(FilterStateIndex::UpstreamServerName,
                                                    via_other_base.get()));
    EXPECT_TRUE(FilterState::isValidObjectForIndex(FilterStateIndex::UpstreamServerName,
                                                   via_canonical_base.get()));

    // 1. Insert via string setData through the non-canonical Object base; FilterStateImpl must
    // re-alias to UpstreamServerName's Object base so typed static_cast getters return the exact
    // UpstreamServerName subobject.
    filterState().setData(key, via_other_base, FilterState::LifeSpan::FilterChain);
    EXPECT_TRUE(filterState().hasIndexedData(FilterStateIndex::UpstreamServerName));
    const auto* ro_sni =
        filterState().getIndexedDataReadOnly<FilterStateIndex::UpstreamServerName>();
    ASSERT_EQ(expected_sni, ro_sni);
    EXPECT_EQ("dual.example.com", ro_sni->value());
    auto* mut_sni = filterState().getIndexedDataMutable<FilterStateIndex::UpstreamServerName>();
    ASSERT_EQ(expected_sni, mut_sni);
    EXPECT_EQ("dual.example.com", mut_sni->value());

    // Both bases remain reachable via dynamic_cast-based lookups.
    const auto* ro_prefix = filterState().getDataReadOnly<DualObjectPrefix>(key);
    ASSERT_EQ(expected_prefix, ro_prefix);
    EXPECT_EQ(42, ro_prefix->prefixValue());
    EXPECT_EQ(expected_sni, filterState().getDataReadOnly<Network::UpstreamServerName>(key));
    EXPECT_EQ(expected_prefix, filterState().getIndexedDataReadOnly<DualObjectPrefix>(
                                   FilterStateIndex::UpstreamServerName));
    EXPECT_EQ(expected_prefix, filterState().getIndexedDataMutable<DualObjectPrefix>(
                                   FilterStateIndex::UpstreamServerName));

    // 2. Insert via legacy runtime-indexed setIndexedData through the non-canonical Object base.
    resetFilterState();
    filterState().setIndexedData(FilterStateIndex::UpstreamServerName, via_other_base,
                                 FilterState::LifeSpan::FilterChain);
    const auto* legacy_ro_sni =
        filterState().getIndexedDataReadOnly<FilterStateIndex::UpstreamServerName>();
    ASSERT_EQ(expected_sni, legacy_ro_sni);
    EXPECT_EQ("dual.example.com", legacy_ro_sni->value());
    EXPECT_EQ(expected_prefix, filterState().getDataReadOnly<DualObjectPrefix>(key));

    // Verify ownership is retained by the aliased shared_ptr inside FilterStateImpl.
    EXPECT_FALSE(destroyed);
  }
  EXPECT_FALSE(destroyed);
  resetFilterState();
  EXPECT_TRUE(destroyed);
}

} // namespace StreamInfo
} // namespace Envoy
