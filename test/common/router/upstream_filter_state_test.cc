#include <memory>
#include <optional>
#include <string>

#include "envoy/router/string_accessor.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/assert.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/router/upstream_filter_state.h"
#include "source/common/stream_info/filter_state_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using StreamInfo::FilterState;
using StreamInfo::FilterStateImpl;
using StreamInfo::FilterStateIndex;
using StreamInfo::FilterStateSharedPtr;

class SimpleIntObject : public FilterState::Object {
public:
  explicit SimpleIntObject(int value) : value_(value) {}

  int value() const { return value_; }
  void setValue(int value) { value_ = value; }

  std::optional<std::string> serializeAsString() const override { return std::to_string(value_); }

private:
  int value_;
};

class UpstreamFilterStateTest : public testing::Test {
public:
  UpstreamFilterStateTest() {
    Assert::resetEnvoyBugCountersForTest();
    downstream_fs_ = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::FilterChain);
    upstream_conn_fs_ = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  }

protected:
  FilterStateSharedPtr downstream_fs_;
  FilterStateSharedPtr upstream_conn_fs_;
};

TEST_F(UpstreamFilterStateTest, ReadThroughHierarchyIsolatesBackendConnectionFromDownstream) {
  downstream_fs_->setData("ds_fc", std::make_unique<StringAccessorImpl>("from_ds_fc"),
                          FilterState::LifeSpan::FilterChain);
  downstream_fs_->setData("ds_req", std::make_unique<StringAccessorImpl>("from_ds_req"),
                          FilterState::LifeSpan::Request);
  downstream_fs_->setData("ds_conn", std::make_unique<StringAccessorImpl>("from_ds_conn"),
                          FilterState::LifeSpan::Connection);
  downstream_fs_->setData("shared_key", std::make_unique<StringAccessorImpl>("from_downstream"),
                          FilterState::LifeSpan::FilterChain);

  auto ufs = std::make_shared<UpstreamFilterState>(downstream_fs_);
  auto upstream_only = ufs->upstreamOnlyFilterState();
  EXPECT_EQ(FilterState::LifeSpan::FilterChain, ufs->lifeSpan());

  // Reads on UpstreamFilterState fall back to downstream across FilterChain, Request, and
  // downstream Connection.
  EXPECT_TRUE(ufs->hasDataWithName("ds_fc"));
  EXPECT_TRUE(ufs->hasDataWithName("ds_req"));
  EXPECT_TRUE(ufs->hasDataWithName("ds_conn"));
  EXPECT_FALSE(ufs->hasDataWithName("non_existent"));
  EXPECT_EQ("from_ds_fc", ufs->getDataReadOnly<StringAccessor>("ds_fc")->asString());
  EXPECT_EQ("from_ds_req", ufs->getDataReadOnly<StringAccessor>("ds_req")->asString());
  EXPECT_EQ("from_ds_conn", ufs->getDataReadOnly<StringAccessor>("ds_conn")->asString());
  EXPECT_EQ("from_downstream", ufs->getDataReadOnly<StringAccessor>("shared_key")->asString());

  // Binding backend connection FilterState does NOT flip or shadow UpstreamFilterState reads,
  // while exposing backend connection entries on upstreamOnlyFilterState().
  upstream_conn_fs_->setData("conn_only", std::make_unique<StringAccessorImpl>("from_conn_only"),
                             FilterState::LifeSpan::Connection);
  upstream_conn_fs_->setData("shared_key",
                             std::make_unique<StringAccessorImpl>("from_upstream_conn"),
                             FilterState::LifeSpan::Connection);
  ufs->bindUpstreamConnectionFilterState(upstream_conn_fs_);

  EXPECT_FALSE(ufs->hasDataWithName("conn_only"));
  EXPECT_EQ("from_downstream", ufs->getDataReadOnly<StringAccessor>("shared_key")->asString());
  EXPECT_TRUE(upstream_only->hasDataWithName("conn_only"));
  EXPECT_EQ("from_conn_only",
            upstream_only->getDataReadOnly<StringAccessor>("conn_only")->asString());
  EXPECT_EQ("from_upstream_conn",
            upstream_only->getDataReadOnly<StringAccessor>("shared_key")->asString());

  // Local attempt FilterChain write shadows both views without mutating backend or downstream.
  ufs->setData("shared_key", std::make_unique<StringAccessorImpl>("from_local_attempt"),
               FilterState::LifeSpan::FilterChain);
  EXPECT_EQ("from_local_attempt", ufs->getDataReadOnly<StringAccessor>("shared_key")->asString());
  EXPECT_EQ("from_local_attempt",
            upstream_only->getDataReadOnly<StringAccessor>("shared_key")->asString());
  EXPECT_EQ("from_upstream_conn",
            upstream_conn_fs_->getDataReadOnly<StringAccessor>("shared_key")->asString());
  EXPECT_EQ("from_downstream",
            downstream_fs_->getDataReadOnly<StringAccessor>("shared_key")->asString());
}

TEST_F(UpstreamFilterStateTest, MutableFallbackToDownstreamAndUpstreamConn) {
  downstream_fs_->setData("ds_mutable", std::make_unique<SimpleIntObject>(10),
                          FilterState::LifeSpan::Request);
  upstream_conn_fs_->setData("conn_mutable", std::make_unique<SimpleIntObject>(30),
                             FilterState::LifeSpan::Connection);

  auto ufs = std::make_shared<UpstreamFilterState>(downstream_fs_);
  auto upstream_only = ufs->upstreamOnlyFilterState();
  ufs->bindUpstreamConnectionFilterState(upstream_conn_fs_);

  // Preserves FilterState invariant on UpstreamFilterState: hasData<T>(k) -> getDataMutable<T>(k).
  ASSERT_TRUE(ufs->hasData<SimpleIntObject>("ds_mutable"));
  SimpleIntObject* ds_mut = ufs->getDataMutable<SimpleIntObject>("ds_mutable");
  ASSERT_NE(nullptr, ds_mut);
  EXPECT_EQ(10, ds_mut->value());
  ds_mut->setValue(15);
  EXPECT_EQ(15, downstream_fs_->getDataReadOnly<SimpleIntObject>("ds_mutable")->value());

  auto ds_shared = ufs->getDataSharedMutableGeneric("ds_mutable");
  ASSERT_NE(nullptr, ds_shared);

  // Backend connection mutable fallback is exposed via upstreamOnlyFilterState(), not
  // UpstreamFilterState.
  EXPECT_FALSE(ufs->hasData<SimpleIntObject>("conn_mutable"));
  EXPECT_EQ(nullptr, ufs->getDataMutable<SimpleIntObject>("conn_mutable"));
  ASSERT_TRUE(upstream_only->hasData<SimpleIntObject>("conn_mutable"));
  SimpleIntObject* conn_mut = upstream_only->getDataMutable<SimpleIntObject>("conn_mutable");
  ASSERT_NE(nullptr, conn_mut);
  conn_mut->setValue(35);
  EXPECT_EQ(35, upstream_conn_fs_->getDataReadOnly<SimpleIntObject>("conn_mutable")->value());

  // Non-existent key returns nullptr.
  EXPECT_EQ(nullptr, ufs->getDataMutable<SimpleIntObject>("missing"));
  EXPECT_EQ(nullptr, ufs->getDataSharedMutableGeneric("missing"));
}

// Failed retry attempts discard FilterChain writes while sharing Request writes across attempts
// on both UpstreamFilterState and upstreamOnlyFilterState().
TEST_F(UpstreamFilterStateTest,
       RetryIsolationDiscardsFailedFilterChainAndSharesRequestAcrossAttempts) {
  downstream_fs_->setData("existing_fc", std::make_unique<StringAccessorImpl>("ds_fc_initial"),
                          FilterState::LifeSpan::FilterChain);
  downstream_fs_->setData("existing_req", std::make_unique<StringAccessorImpl>("ds_req_initial"),
                          FilterState::LifeSpan::Request);

  auto shared_req_fs = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Request);
  auto attempt1 = std::make_shared<UpstreamFilterState>(downstream_fs_, shared_req_fs);
  auto attempt1_up_only = attempt1->upstreamOnlyFilterState();
  attempt1->setData("existing_fc", std::make_unique<StringAccessorImpl>("attempt1_fc_override"),
                    FilterState::LifeSpan::FilterChain);
  attempt1->setData("existing_req", std::make_unique<StringAccessorImpl>("attempt1_req_override"),
                    FilterState::LifeSpan::Request);
  attempt1->setData("attempt1_only_fc", std::make_unique<StringAccessorImpl>("attempt1_fc_val"),
                    FilterState::LifeSpan::FilterChain);
  attempt1->setData("attempt1_only_req", std::make_unique<StringAccessorImpl>("attempt1_req_val"),
                    FilterState::LifeSpan::Request);

  // Within Attempt 1, both views see the staged FilterChain and Request entries.
  EXPECT_EQ("attempt1_fc_override",
            attempt1->getDataReadOnly<StringAccessor>("existing_fc")->asString());
  EXPECT_EQ("attempt1_req_override",
            attempt1->getDataReadOnly<StringAccessor>("existing_req")->asString());
  EXPECT_EQ("attempt1_fc_val",
            attempt1_up_only->getDataReadOnly<StringAccessor>("attempt1_only_fc")->asString());
  EXPECT_EQ("attempt1_req_val",
            attempt1_up_only->getDataReadOnly<StringAccessor>("attempt1_only_req")->asString());

  // Attempt 1 fails without commitToDownstream(): FilterChain writes are discarded, while Request
  // writes persisted to downstream_fs_ and shared_req_fs for Attempt 2.
  EXPECT_EQ("ds_fc_initial",
            downstream_fs_->getDataReadOnly<StringAccessor>("existing_fc")->asString());
  EXPECT_EQ("attempt1_req_override",
            downstream_fs_->getDataReadOnly<StringAccessor>("existing_req")->asString());
  EXPECT_FALSE(downstream_fs_->hasDataWithName("attempt1_only_fc"));
  EXPECT_EQ("attempt1_req_val",
            downstream_fs_->getDataReadOnly<StringAccessor>("attempt1_only_req")->asString());

  auto attempt2 = std::make_shared<UpstreamFilterState>(downstream_fs_, shared_req_fs);
  auto attempt2_up_only = attempt2->upstreamOnlyFilterState();
  EXPECT_EQ("ds_fc_initial", attempt2->getDataReadOnly<StringAccessor>("existing_fc")->asString());
  EXPECT_EQ("attempt1_req_override",
            attempt2->getDataReadOnly<StringAccessor>("existing_req")->asString());
  EXPECT_FALSE(attempt2->hasDataWithName("attempt1_only_fc"));
  EXPECT_EQ("attempt1_req_val",
            attempt2->getDataReadOnly<StringAccessor>("attempt1_only_req")->asString());
  EXPECT_FALSE(attempt2_up_only->hasDataWithName("attempt1_only_fc"));
  EXPECT_EQ("attempt1_req_val",
            attempt2_up_only->getDataReadOnly<StringAccessor>("attempt1_only_req")->asString());
}

TEST_F(UpstreamFilterStateTest, ClientVsBackendConnectionLifeSpanSplit) {
  auto ufs = std::make_shared<UpstreamFilterState>(downstream_fs_);
  auto upstream_only = ufs->upstreamOnlyFilterState();

  // LifeSpan::Connection and TopSpan on UpstreamFilterState write directly to downstream_fs_ at
  // LifeSpan::Connection without exposing client connection keys on upstreamOnlyFilterState().
  ufs->setData("client_conn", std::make_unique<SimpleIntObject>(42),
               FilterState::LifeSpan::Connection);
  ufs->setData("client_top", std::make_unique<SimpleIntObject>(99), FilterState::LifeSpan::TopSpan);
  EXPECT_TRUE(ufs->hasDataWithName("client_conn"));
  EXPECT_TRUE(ufs->hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));
  EXPECT_EQ(
      42,
      downstream_fs_->parent()->parent()->getDataReadOnly<SimpleIntObject>("client_conn")->value());
  EXPECT_EQ(
      99,
      downstream_fs_->parent()->parent()->getDataReadOnly<SimpleIntObject>("client_top")->value());
  EXPECT_FALSE(upstream_only->hasDataWithName("client_conn"));
  EXPECT_FALSE(upstream_only->hasDataWithName("client_top"));

  // Backend LifeSpan::Connection and TopSpan via upstreamOnlyFilterState() lazily allocate and
  // stage in pre_pool_conn_filter_state_ without touching downstream_fs_ or UpstreamFilterState.
  upstream_only->setData("pre_pool_conn", std::make_unique<SimpleIntObject>(100),
                         FilterState::LifeSpan::Connection);
  upstream_only->setData("pre_pool_top", std::make_unique<SimpleIntObject>(200),
                         FilterState::LifeSpan::TopSpan);
  upstream_only->setData("client_conn", std::make_unique<SimpleIntObject>(777),
                         FilterState::LifeSpan::Connection);

  EXPECT_TRUE(upstream_only->hasDataWithName("pre_pool_conn"));
  EXPECT_TRUE(upstream_only->hasDataWithName("pre_pool_top"));
  EXPECT_TRUE(upstream_only->hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));
  EXPECT_EQ(100, upstream_only->getDataReadOnly<SimpleIntObject>("pre_pool_conn")->value());
  upstream_only->getDataMutable<SimpleIntObject>("pre_pool_conn")->setValue(101);
  EXPECT_EQ(101, upstream_only->getDataReadOnly<SimpleIntObject>("pre_pool_conn")->value());
  EXPECT_EQ(777, upstream_only->getDataReadOnly<SimpleIntObject>("client_conn")->value());

  // Downstream and UpstreamFilterState do not see backend Connection writes, and client_conn does
  // not flip.
  EXPECT_FALSE(downstream_fs_->hasDataWithName("pre_pool_conn"));
  EXPECT_FALSE(downstream_fs_->hasDataWithName("pre_pool_top"));
  EXPECT_FALSE(ufs->hasDataWithName("pre_pool_conn"));
  EXPECT_EQ(42, ufs->getDataReadOnly<SimpleIntObject>("client_conn")->value());

  // Bind upstream connection FilterState; pre-pool backend entries merge into upstream_conn_fs_
  // while UpstreamFilterState continues reading client_conn = 42 from downstream_fs_ without
  // flipping.
  ufs->bindUpstreamConnectionFilterState(upstream_conn_fs_);
  EXPECT_TRUE(upstream_conn_fs_->hasDataWithName("pre_pool_conn"));
  EXPECT_TRUE(upstream_conn_fs_->hasDataWithName("pre_pool_top"));
  EXPECT_EQ(101, upstream_conn_fs_->getDataReadOnly<SimpleIntObject>("pre_pool_conn")->value());
  EXPECT_EQ(200, upstream_conn_fs_->getDataReadOnly<SimpleIntObject>("pre_pool_top")->value());
  EXPECT_EQ(777, upstream_conn_fs_->getDataReadOnly<SimpleIntObject>("client_conn")->value());
  EXPECT_EQ(42, ufs->getDataReadOnly<SimpleIntObject>("client_conn")->value());

  // Subsequent backend LifeSpan::Connection writes on upstreamOnlyFilterState() go directly to
  // upstream_conn_fs_.
  upstream_only->setData("post_pool_conn", std::make_unique<SimpleIntObject>(300),
                         FilterState::LifeSpan::Connection);
  upstream_only->setData("pre_pool_top", std::make_unique<SimpleIntObject>(205),
                         FilterState::LifeSpan::Connection);
  EXPECT_EQ(300, upstream_conn_fs_->getDataReadOnly<SimpleIntObject>("post_pool_conn")->value());
  EXPECT_EQ(205, upstream_conn_fs_->getDataReadOnly<SimpleIntObject>("pre_pool_top")->value());

  // Committing to downstream does not copy backend Connection entries to downstream_fs_ or
  // downstream Connection entries to upstream_conn_fs_.
  ufs->commitToDownstream();
  EXPECT_FALSE(downstream_fs_->hasDataWithName("pre_pool_conn"));
  EXPECT_FALSE(downstream_fs_->hasDataWithName("pre_pool_top"));
  EXPECT_FALSE(downstream_fs_->hasDataWithName("post_pool_conn"));
  EXPECT_FALSE(upstream_conn_fs_->hasDataWithName("client_top"));
}

TEST_F(UpstreamFilterStateTest, CommitToDownstreamMergesFilterChainAndPreservesRequestLifespan) {
  downstream_fs_->setData("ds_fc", std::make_unique<StringAccessorImpl>("ds_fc_init"),
                          FilterState::LifeSpan::FilterChain);
  downstream_fs_->setData("ds_req", std::make_unique<StringAccessorImpl>("ds_req_init"),
                          FilterState::LifeSpan::Request);
  downstream_fs_->setData("untouched", std::make_unique<StringAccessorImpl>("ds_untouched"),
                          FilterState::LifeSpan::Request);

  UpstreamFilterState ufs(downstream_fs_);
  ufs.setData("ds_fc", std::make_unique<StringAccessorImpl>("up_fc_override"),
              FilterState::LifeSpan::FilterChain);
  ufs.setData("ds_req", std::make_unique<StringAccessorImpl>("up_req_override"),
              FilterState::LifeSpan::Request);
  ufs.setData("new_fc", std::make_unique<StringAccessorImpl>("up_new_fc"),
              FilterState::LifeSpan::FilterChain);
  ufs.setData("new_req", std::make_unique<StringAccessorImpl>("up_new_req"),
              FilterState::LifeSpan::Request);

  // Request writes write through immediately, whereas FilterChain writes stage until commit.
  EXPECT_EQ("ds_fc_init", downstream_fs_->getDataReadOnly<StringAccessor>("ds_fc")->asString());
  EXPECT_FALSE(downstream_fs_->hasDataWithName("new_fc"));
  EXPECT_EQ("up_req_override",
            downstream_fs_->getDataReadOnly<StringAccessor>("ds_req")->asString());
  EXPECT_EQ("up_new_req", downstream_fs_->getDataReadOnly<StringAccessor>("new_req")->asString());

  EXPECT_FALSE(ufs.committedToDownstream());
  ufs.commitToDownstream();
  EXPECT_TRUE(ufs.committedToDownstream());

  // Calling commitToDownstream() again is an idempotent no-op.
  ufs.commitToDownstream();

  EXPECT_EQ("up_fc_override", downstream_fs_->getDataReadOnly<StringAccessor>("ds_fc")->asString());
  EXPECT_EQ("up_req_override",
            downstream_fs_->getDataReadOnly<StringAccessor>("ds_req")->asString());
  EXPECT_EQ("up_new_fc", downstream_fs_->getDataReadOnly<StringAccessor>("new_fc")->asString());
  EXPECT_EQ("up_new_req", downstream_fs_->getDataReadOnly<StringAccessor>("new_req")->asString());
  EXPECT_EQ("ds_untouched",
            downstream_fs_->getDataReadOnly<StringAccessor>("untouched")->asString());

  // Verify that Request-scoped entries survive recreateStream (internal redirect), whereas
  // FilterChain entries do not.
  FilterStateImpl recreated_fs(downstream_fs_->parent(), FilterState::LifeSpan::FilterChain);
  EXPECT_EQ("up_req_override", recreated_fs.getDataReadOnly<StringAccessor>("ds_req")->asString());
  EXPECT_EQ("up_new_req", recreated_fs.getDataReadOnly<StringAccessor>("new_req")->asString());
  EXPECT_EQ("ds_untouched", recreated_fs.getDataReadOnly<StringAccessor>("untouched")->asString());
  EXPECT_FALSE(recreated_fs.hasDataWithName("ds_fc"));
  EXPECT_FALSE(recreated_fs.hasDataWithName("new_fc"));
}

TEST_F(UpstreamFilterStateTest, CommitToDownstreamWhenDownstreamHasConnectionLifeSpan) {
  // In TcpProxy tunneling, downstream_fs_ itself has LifeSpan::Connection.
  auto tcp_downstream_fs = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Connection);
  tcp_downstream_fs->setData("existing_key", std::make_unique<StringAccessorImpl>("initial"),
                             FilterState::LifeSpan::Connection);

  UpstreamFilterState ufs(tcp_downstream_fs);
  ufs.setData("existing_key", std::make_unique<StringAccessorImpl>("overwritten"),
              FilterState::LifeSpan::FilterChain);
  ufs.setData("new_key", std::make_unique<StringAccessorImpl>("added"),
              FilterState::LifeSpan::Request);

  ufs.commitToDownstream();
  EXPECT_EQ("overwritten",
            tcp_downstream_fs->getDataReadOnly<StringAccessor>("existing_key")->asString());
  EXPECT_EQ("added", tcp_downstream_fs->getDataReadOnly<StringAccessor>("new_key")->asString());

  // Post-commit write-through also works when downstream is Connection-scoped.
  ufs.setData("post_commit_key", std::make_unique<StringAccessorImpl>("post_commit"),
              FilterState::LifeSpan::FilterChain);
  EXPECT_EQ("post_commit",
            tcp_downstream_fs->getDataReadOnly<StringAccessor>("post_commit_key")->asString());
}

TEST_F(UpstreamFilterStateTest, PostCommitWriteThrough) {
  downstream_fs_->setData("key_fc", std::make_unique<StringAccessorImpl>("initial_fc"),
                          FilterState::LifeSpan::FilterChain);
  downstream_fs_->setData("key_req", std::make_unique<StringAccessorImpl>("initial_req"),
                          FilterState::LifeSpan::Request);

  UpstreamFilterState ufs(downstream_fs_);
  ufs.setData("key_fc", std::make_unique<StringAccessorImpl>("pre_commit_fc"),
              FilterState::LifeSpan::FilterChain);
  ufs.commitToDownstream();
  EXPECT_EQ("pre_commit_fc", downstream_fs_->getDataReadOnly<StringAccessor>("key_fc")->asString());

  // Post-commit writes to existing keys and new keys write through immediately at their lifespan.
  ufs.setData("key_fc", std::make_unique<StringAccessorImpl>("post_commit_fc"),
              FilterState::LifeSpan::FilterChain);
  ufs.setData("key_req", std::make_unique<StringAccessorImpl>("post_commit_req"),
              FilterState::LifeSpan::Request);
  ufs.setData("new_fc", std::make_unique<StringAccessorImpl>("new_post_fc"),
              FilterState::LifeSpan::FilterChain);
  ufs.setData("new_req", std::make_unique<StringAccessorImpl>("new_post_req"),
              FilterState::LifeSpan::Request);

  EXPECT_EQ("post_commit_fc",
            downstream_fs_->getDataReadOnly<StringAccessor>("key_fc")->asString());
  EXPECT_EQ("post_commit_req",
            downstream_fs_->getDataReadOnly<StringAccessor>("key_req")->asString());
  EXPECT_EQ("new_post_fc", downstream_fs_->getDataReadOnly<StringAccessor>("new_fc")->asString());
  EXPECT_EQ("new_post_req", downstream_fs_->getDataReadOnly<StringAccessor>("new_req")->asString());

  FilterStateImpl recreated_fs(downstream_fs_->parent(), FilterState::LifeSpan::FilterChain);
  EXPECT_EQ("post_commit_req", recreated_fs.getDataReadOnly<StringAccessor>("key_req")->asString());
  EXPECT_EQ("new_post_req", recreated_fs.getDataReadOnly<StringAccessor>("new_req")->asString());
  EXPECT_FALSE(recreated_fs.hasDataWithName("new_fc"));
}

TEST_F(UpstreamFilterStateTest, IndexedDataFullLifecycle) {
  constexpr auto kServerNameIdx = FilterStateIndex::UpstreamServerName;
  constexpr auto kLocalReplyIdx = FilterStateIndex::LocalReplyOwner;
  constexpr auto kConnectPortIdx = FilterStateIndex::OriginalConnectPort;

  downstream_fs_->setIndexedData(kServerNameIdx,
                                 std::make_unique<StringAccessorImpl>("ds.example.com"),
                                 FilterState::LifeSpan::FilterChain);
  downstream_fs_->setIndexedData(kLocalReplyIdx, std::make_unique<SimpleIntObject>(1),
                                 FilterState::LifeSpan::Request);

  auto shared_req_fs = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Request);
  auto attempt1 = std::make_shared<UpstreamFilterState>(downstream_fs_, shared_req_fs);
  auto attempt1_up_only = attempt1->upstreamOnlyFilterState();
  // Read-through for indexed data.
  EXPECT_TRUE(attempt1->hasIndexedData(kServerNameIdx));
  EXPECT_TRUE(attempt1->hasDataWithName("envoy.network.upstream_server_name"));
  EXPECT_EQ("ds.example.com",
            attempt1->getIndexedDataReadOnly<StringAccessor>(kServerNameIdx)->asString());

  // Mutable fallback for indexed data.
  ASSERT_NE(nullptr, attempt1->getIndexedDataMutable<SimpleIntObject>(kLocalReplyIdx));
  ASSERT_NE(nullptr, attempt1->getIndexedDataSharedMutableGeneric(kLocalReplyIdx));
  attempt1->getIndexedDataMutable<SimpleIntObject>(kLocalReplyIdx)->setValue(2);
  EXPECT_EQ(2, downstream_fs_->getIndexedDataReadOnly<SimpleIntObject>(kLocalReplyIdx)->value());

  // Failed attempt stages FilterChain indexed writes locally and discards them, while Request
  // indexed writes write through immediately and share across attempts.
  attempt1->setIndexedData(kServerNameIdx,
                           std::make_unique<StringAccessorImpl>("attempt1.example.com"),
                           FilterState::LifeSpan::FilterChain);
  attempt1->setIndexedData(kLocalReplyIdx, std::make_unique<SimpleIntObject>(10),
                           FilterState::LifeSpan::Request);
  EXPECT_EQ("attempt1.example.com",
            attempt1->getIndexedDataReadOnly<StringAccessor>(kServerNameIdx)->asString());
  EXPECT_EQ(10, attempt1->getIndexedDataReadOnly<SimpleIntObject>(kLocalReplyIdx)->value());
  EXPECT_EQ("ds.example.com",
            downstream_fs_->getIndexedDataReadOnly<StringAccessor>(kServerNameIdx)->asString());
  EXPECT_EQ(10, downstream_fs_->getIndexedDataReadOnly<SimpleIntObject>(kLocalReplyIdx)->value());

  // Backend Connection lifespan buffering & binding via upstreamOnlyFilterState() vs downstream
  // Connection lifespan on UpstreamFilterState.
  attempt1_up_only->setIndexedData(kConnectPortIdx, std::make_unique<SimpleIntObject>(8443),
                                   FilterState::LifeSpan::Connection);
  EXPECT_TRUE(attempt1_up_only->hasIndexedData(kConnectPortIdx));
  EXPECT_FALSE(attempt1->hasIndexedData(kConnectPortIdx));
  EXPECT_EQ(8443,
            attempt1_up_only->getIndexedDataReadOnly<SimpleIntObject>(kConnectPortIdx)->value());
  attempt1->bindUpstreamConnectionFilterState(upstream_conn_fs_);
  EXPECT_TRUE(upstream_conn_fs_->hasIndexedData(kConnectPortIdx));
  EXPECT_EQ(8443,
            upstream_conn_fs_->getIndexedDataReadOnly<SimpleIntObject>(kConnectPortIdx)->value());

  attempt1->setIndexedData(kConnectPortIdx, std::make_unique<SimpleIntObject>(443),
                           FilterState::LifeSpan::Connection);
  EXPECT_EQ(443, attempt1->getIndexedDataReadOnly<SimpleIntObject>(kConnectPortIdx)->value());
  EXPECT_EQ(443, downstream_fs_->parent()
                     ->parent()
                     ->getIndexedDataReadOnly<SimpleIntObject>(kConnectPortIdx)
                     ->value());
  EXPECT_EQ(8443,
            upstream_conn_fs_->getIndexedDataReadOnly<SimpleIntObject>(kConnectPortIdx)->value());

  // Attempt 2 sees Attempt 1's Request indexed write on both views, discards Attempt 1's
  // FilterChain indexed write, commits its own FilterChain write, and writes through post-commit.
  auto attempt2 = std::make_shared<UpstreamFilterState>(downstream_fs_, shared_req_fs);
  auto attempt2_up_only = attempt2->upstreamOnlyFilterState();
  EXPECT_EQ("ds.example.com",
            attempt2->getIndexedDataReadOnly<StringAccessor>(kServerNameIdx)->asString());
  EXPECT_EQ(10, attempt2->getIndexedDataReadOnly<SimpleIntObject>(kLocalReplyIdx)->value());
  EXPECT_EQ(10, attempt2_up_only->getIndexedDataReadOnly<SimpleIntObject>(kLocalReplyIdx)->value());
  attempt2->setIndexedData(kServerNameIdx,
                           std::make_unique<StringAccessorImpl>("attempt2.example.com"),
                           FilterState::LifeSpan::FilterChain);
  attempt2->setIndexedData(kLocalReplyIdx, std::make_unique<SimpleIntObject>(20),
                           FilterState::LifeSpan::Request);
  attempt2->commitToDownstream();
  EXPECT_EQ("attempt2.example.com",
            downstream_fs_->getIndexedDataReadOnly<StringAccessor>(kServerNameIdx)->asString());
  EXPECT_EQ(20, downstream_fs_->getIndexedDataReadOnly<SimpleIntObject>(kLocalReplyIdx)->value());

  // Post-commit write-through for indexed data (both FilterChain and Request).
  attempt2->setIndexedData(kServerNameIdx,
                           std::make_unique<StringAccessorImpl>("postcommit-fc.example.com"),
                           FilterState::LifeSpan::FilterChain);
  EXPECT_EQ("postcommit-fc.example.com",
            downstream_fs_->getIndexedDataReadOnly<StringAccessor>(kServerNameIdx)->asString());
  attempt2->setIndexedData(kLocalReplyIdx, std::make_unique<SimpleIntObject>(30),
                           FilterState::LifeSpan::Request);
  EXPECT_EQ(30, downstream_fs_->getIndexedDataReadOnly<SimpleIntObject>(kLocalReplyIdx)->value());
  EXPECT_EQ(30, attempt2->getIndexedDataReadOnly<SimpleIntObject>(kLocalReplyIdx)->value());
}

TEST_F(UpstreamFilterStateTest, StreamSharingParameterIsIgnoredOnUpstreamFilterState) {
  downstream_fs_->setData("ds_shared", std::make_shared<SimpleIntObject>(1),
                          FilterState::LifeSpan::Request,
                          StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);

  auto ufs = std::make_shared<UpstreamFilterState>(downstream_fs_);
  auto upstream_only = ufs->upstreamOnlyFilterState();

  // Upstream writes ignore stream_sharing across all lifespans because upstream transport socket
  // options and connection pool keys are finalized before the upstream filter chain is created.
  ufs->setData("up_fc_shared", std::make_shared<SimpleIntObject>(10),
               FilterState::LifeSpan::FilterChain,
               StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  ufs->setData("up_req_shared", std::make_shared<SimpleIntObject>(20),
               FilterState::LifeSpan::Request,
               StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  ufs->setData("up_conn_shared", std::make_shared<SimpleIntObject>(30),
               FilterState::LifeSpan::Connection,
               StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  upstream_only->setIndexedData(
      FilterStateIndex::UpstreamServerName, std::make_shared<StringAccessorImpl>("up.example.com"),
      FilterState::LifeSpan::FilterChain,
      StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);

  auto shared_objects = ufs->objectsSharedWithUpstreamConnection();
  ASSERT_EQ(1u, shared_objects->size());
  EXPECT_EQ("ds_shared", (*shared_objects)[0].name_);
  EXPECT_TRUE(upstream_only->objectsSharedWithUpstreamConnection()->empty());

  ufs->commitToDownstream();
  auto ds_shared_after_commit = downstream_fs_->objectsSharedWithUpstreamConnection();
  ASSERT_EQ(1u, ds_shared_after_commit->size());
  EXPECT_EQ("ds_shared", (*ds_shared_after_commit)[0].name_);
}

TEST_F(UpstreamFilterStateTest, UpstreamOnlyFilterStateDoesNotFallbackToDownstream) {
  downstream_fs_->setData("ds_only_fc", std::make_unique<StringAccessorImpl>("ds_fc_val"),
                          FilterState::LifeSpan::FilterChain);
  downstream_fs_->setData("ds_only_req", std::make_unique<StringAccessorImpl>("ds_req_val"),
                          FilterState::LifeSpan::Request);
  downstream_fs_->setData("ds_only_conn", std::make_unique<StringAccessorImpl>("ds_conn_val"),
                          FilterState::LifeSpan::Connection,
                          StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  downstream_fs_->setIndexedData(FilterStateIndex::LocalReplyOwner,
                                 std::make_unique<SimpleIntObject>(42),
                                 FilterState::LifeSpan::Request);

  auto ufs = std::make_shared<UpstreamFilterState>(downstream_fs_);
  auto upstream_only = ufs->upstreamOnlyFilterState();
  ASSERT_NE(nullptr, upstream_only);
  EXPECT_EQ(FilterState::LifeSpan::FilterChain, upstream_only->lifeSpan());

  // Upstream-only view does NOT see downstream-only keys or indexed data.
  EXPECT_FALSE(upstream_only->hasDataWithName("ds_only_fc"));
  EXPECT_FALSE(upstream_only->hasDataWithName("ds_only_req"));
  EXPECT_FALSE(upstream_only->hasDataWithName("ds_only_conn"));
  EXPECT_EQ(nullptr, upstream_only->getDataReadOnly<StringAccessor>("ds_only_fc"));
  EXPECT_EQ(nullptr, upstream_only->getDataMutable<StringAccessor>("ds_only_fc"));
  EXPECT_EQ(nullptr, upstream_only->getDataSharedMutableGeneric("ds_only_fc"));
  EXPECT_FALSE(upstream_only->hasIndexedData(FilterStateIndex::LocalReplyOwner));
  EXPECT_EQ(nullptr, upstream_only->getIndexedDataReadOnly<SimpleIntObject>(
                         FilterStateIndex::LocalReplyOwner));
  EXPECT_EQ(nullptr, upstream_only->getIndexedDataMutable<SimpleIntObject>(
                         FilterStateIndex::LocalReplyOwner));
  EXPECT_FALSE(upstream_only->hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::FilterChain));
  EXPECT_TRUE(upstream_only->objectsSharedWithUpstreamConnection()->empty());

  // Writes on UpstreamFilterState (FilterChain, Request) and via upstream_only (FilterChain,
  // Request, backend Connection) are visible through upstream_only. Request writes write through
  // immediately to downstream_fs_ and FilterChain writes stage locally until commitToDownstream(),
  // while downstream Connection writes on UpstreamFilterState stay on downstream_fs_ only and
  // backend Connection writes on upstream_only stay on upstream_conn_fs_ only.
  ufs->setData("up_fc", std::make_unique<StringAccessorImpl>("up_fc_val"),
               FilterState::LifeSpan::FilterChain);
  upstream_only->setData("up_req", std::make_unique<StringAccessorImpl>("up_req_val"),
                         FilterState::LifeSpan::Request,
                         StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  upstream_only->setData("up_conn", std::make_unique<StringAccessorImpl>("up_conn_val"),
                         FilterState::LifeSpan::Connection);
  ufs->setData("ds_conn_from_ufs", std::make_unique<StringAccessorImpl>("ds_conn_from_ufs_val"),
               FilterState::LifeSpan::Connection);
  upstream_only->setIndexedData(FilterStateIndex::UpstreamServerName,
                                std::make_unique<StringAccessorImpl>("up.example.com"),
                                FilterState::LifeSpan::FilterChain);

  EXPECT_TRUE(upstream_only->hasDataWithName("up_fc"));
  EXPECT_TRUE(upstream_only->hasDataWithName("up_req"));
  EXPECT_TRUE(upstream_only->hasDataWithName("up_conn"));
  EXPECT_FALSE(upstream_only->hasDataWithName("ds_conn_from_ufs"));
  EXPECT_TRUE(downstream_fs_->hasDataWithName("ds_conn_from_ufs"));
  EXPECT_TRUE(downstream_fs_->hasDataWithName("up_req"));
  EXPECT_FALSE(downstream_fs_->hasDataWithName("up_fc"));
  EXPECT_FALSE(downstream_fs_->hasIndexedData(FilterStateIndex::UpstreamServerName));
  EXPECT_EQ("up_fc_val", upstream_only->getDataReadOnly<StringAccessor>("up_fc")->asString());
  EXPECT_EQ("up_req_val", upstream_only->getDataReadOnly<StringAccessor>("up_req")->asString());
  EXPECT_EQ("up_conn_val", upstream_only->getDataReadOnly<StringAccessor>("up_conn")->asString());
  EXPECT_NE(nullptr, upstream_only->getDataMutable<StringAccessor>("up_fc"));
  EXPECT_TRUE(upstream_only->hasIndexedData(FilterStateIndex::UpstreamServerName));
  EXPECT_EQ(
      "up.example.com",
      upstream_only->getIndexedDataReadOnly<StringAccessor>(FilterStateIndex::UpstreamServerName)
          ->asString());
  EXPECT_NE(nullptr, upstream_only->getIndexedDataMutable<StringAccessor>(
                         FilterStateIndex::UpstreamServerName));
  EXPECT_TRUE(upstream_only->hasDataAtOrAboveLifeSpan(FilterState::LifeSpan::Connection));
  EXPECT_TRUE(upstream_only->objectsSharedWithUpstreamConnection()->empty());

  // Bind upstream connection filter state and commit to downstream; upstream_only still excludes
  // downstream-only keys while staged FilterChain writes merge into downstream_fs_ alongside the
  // earlier immediate Request writes.
  ufs->bindUpstreamConnectionFilterState(upstream_conn_fs_);
  EXPECT_EQ(upstream_conn_fs_.get(), upstream_only->parent().get());
  ufs->commitToDownstream();

  upstream_only->setData("post_commit_up", std::make_unique<StringAccessorImpl>("post_commit_val"),
                         FilterState::LifeSpan::FilterChain);
  EXPECT_FALSE(upstream_only->hasDataWithName("ds_only_fc"));
  EXPECT_FALSE(upstream_only->hasDataWithName("ds_only_req"));
  EXPECT_TRUE(upstream_only->hasDataWithName("up_fc"));
  EXPECT_TRUE(upstream_only->hasDataWithName("up_req"));
  EXPECT_TRUE(upstream_only->hasDataWithName("up_conn"));
  EXPECT_TRUE(upstream_only->hasDataWithName("post_commit_up"));
  EXPECT_TRUE(downstream_fs_->hasDataWithName("up_fc"));
  EXPECT_TRUE(downstream_fs_->hasDataWithName("up_req"));
  EXPECT_TRUE(downstream_fs_->hasDataWithName("post_commit_up"));
  EXPECT_FALSE(downstream_fs_->hasDataWithName("up_conn"));
  EXPECT_TRUE(downstream_fs_->hasIndexedData(FilterStateIndex::UpstreamServerName));
}

TEST_F(UpstreamFilterStateTest, CrossTierLifeSpanCollisionsTriggerEnvoyBug) {
  // 1. Local FilterChain <-> Request collisions on UpstreamFilterState.
  {
    UpstreamFilterState ufs(downstream_fs_);
    ufs.setData("local_key", std::make_unique<SimpleIntObject>(1),
                FilterState::LifeSpan::FilterChain);
    EXPECT_ENVOY_BUG(ufs.setData("local_key", std::make_unique<SimpleIntObject>(2),
                                 FilterState::LifeSpan::Request),
                     "FilterStateAccessViolation");
  }
  {
    UpstreamFilterState ufs(downstream_fs_);
    ufs.setData("local_req", std::make_unique<SimpleIntObject>(1), FilterState::LifeSpan::Request);
    EXPECT_ENVOY_BUG(ufs.setData("local_req", std::make_unique<SimpleIntObject>(2),
                                 FilterState::LifeSpan::FilterChain),
                     "FilterStateAccessViolation");
  }

  // 2. Local FilterChain/Request followed by Connection write on UpstreamFilterState (named and
  // indexed).
  {
    UpstreamFilterState ufs(downstream_fs_);
    ufs.setData("fc_then_conn", std::make_unique<SimpleIntObject>(1),
                FilterState::LifeSpan::FilterChain);
    EXPECT_ENVOY_BUG(ufs.setData("fc_then_conn", std::make_unique<SimpleIntObject>(2),
                                 FilterState::LifeSpan::Connection),
                     "FilterStateAccessViolation");

    ufs.setIndexedData(FilterStateIndex::UpstreamServerName, std::make_unique<SimpleIntObject>(1),
                       FilterState::LifeSpan::Request);
    EXPECT_ENVOY_BUG(ufs.setIndexedData(FilterStateIndex::UpstreamServerName,
                                        std::make_unique<SimpleIntObject>(2),
                                        FilterState::LifeSpan::Connection),
                     "FilterStateAccessViolation");
  }

  // 3. Downstream Request/Connection entry collided by upstream FilterChain write at commit time
  // and post-commit write-through.
  {
    auto ds = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::FilterChain);
    ds->setData("ds_req_key", std::make_unique<SimpleIntObject>(1), FilterState::LifeSpan::Request);
    UpstreamFilterState ufs(ds);
    ufs.setData("ds_req_key", std::make_unique<SimpleIntObject>(2),
                FilterState::LifeSpan::FilterChain);
    EXPECT_ENVOY_BUG(ufs.commitToDownstream(), "FilterStateAccessViolation");
  }
  {
    auto ds = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::FilterChain);
    ds->setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_unique<SimpleIntObject>(1),
                       FilterState::LifeSpan::Connection);
    UpstreamFilterState ufs(ds);
    EXPECT_ENVOY_BUG(ufs.setIndexedData(FilterStateIndex::LocalReplyOwner,
                                        std::make_unique<SimpleIntObject>(2),
                                        FilterState::LifeSpan::Request),
                     "FilterStateAccessViolation");
    EXPECT_EQ(
        1, ufs.getIndexedDataReadOnly<SimpleIntObject>(FilterStateIndex::LocalReplyOwner)->value());
    ufs.commitToDownstream();
    EXPECT_ENVOY_BUG(ufs.setIndexedData(FilterStateIndex::LocalReplyOwner,
                                        std::make_unique<SimpleIntObject>(2),
                                        FilterState::LifeSpan::FilterChain),
                     "FilterStateAccessViolation");
  }
  {
    // Cross-attempt Request -> FilterChain collision via shared_request_lifespan_filter_state.
    auto ds = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::FilterChain);
    auto shared_req_fs = std::make_shared<FilterStateImpl>(FilterState::LifeSpan::Request);
    UpstreamFilterState attempt1(ds, shared_req_fs);
    attempt1.setData("cross_attempt_req", std::make_unique<SimpleIntObject>(1),
                     FilterState::LifeSpan::Request);
    UpstreamFilterState attempt2(ds, shared_req_fs);
    EXPECT_ENVOY_BUG(attempt2.setData("cross_attempt_req", std::make_unique<SimpleIntObject>(2),
                                      FilterState::LifeSpan::FilterChain),
                     "FilterStateAccessViolation");
  }
}

} // namespace
} // namespace Router
} // namespace Envoy
