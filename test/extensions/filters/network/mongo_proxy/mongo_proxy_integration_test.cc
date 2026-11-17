#include "source/common/buffer/buffer_impl.h"
#include "source/common/config/well_known_names.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/thread_local_store.h"
#include "source/extensions/filters/network/mongo_proxy/bson_impl.h"
#include "source/extensions/filters/network/mongo_proxy/codec_impl.h"

#include "test/integration/integration.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace {

using Extensions::NetworkFilters::MongoProxy::EncoderImpl;
using Extensions::NetworkFilters::MongoProxy::QueryMessageImpl;
using Extensions::NetworkFilters::MongoProxy::ReplyMessageImpl;
namespace Bson = Extensions::NetworkFilters::MongoProxy::Bson;

class MongoProxyIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public BaseIntegrationTest {
public:
  MongoProxyIntegrationTest() : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

  // Whether the stats store derives tags with the explicit-tags logic (the tag-friendly scope API)
  // rather than with the legacy tag-extraction rules. Both modes must produce identical stat
  // names, tag-extracted names and tags for all the mongo stats.
  bool explicitTags() const { return version_ == Network::Address::IpVersion::v4; }

  void initialize() override { initializeWithPrefix("mongo_stats"); }

  void initializeWithPrefix(absl::string_view stat_prefix) {
    config_helper_.addNetworkFilter(absl::StrCat(R"EOF(
name: mongo
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.mongo_proxy.v3.MongoProxy
  stat_prefix: ")EOF",
                                                 stat_prefix, "\"\n"));
    // Exercise both stats modes without doubling the test matrix: the two IP versions run the
    // server in different modes. The mode is read during server initialization, before the
    // runtime loader exists, so it has to be set directly rather than with addRuntimeOverride().
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.enable_stats_explicit_tags",
                                  explicitTags());
    BaseIntegrationTest::initialize();

    // Sanity check that the parameterized mode really took effect; otherwise both IP versions
    // would silently be exercising the same thing.
    auto* store = dynamic_cast<Stats::ThreadLocalStoreImpl*>(&test_server_->statStore());
    ASSERT_NE(store, nullptr);
    EXPECT_EQ(store->useExplicitTags(), explicitTags());
  }

  std::string encodeQuery(int32_t request_id, const std::string& full_collection_name,
                          Bson::DocumentSharedPtr query) {
    Buffer::OwnedImpl output;
    EncoderImpl encoder(output);
    QueryMessageImpl message(request_id, 0);
    message.fullCollectionName(full_collection_name);
    message.query(std::move(query));
    encoder.encodeQuery(message);
    return output.toString();
  }

  std::string encodeReply(int32_t response_to) {
    Buffer::OwnedImpl output;
    EncoderImpl encoder(output);
    ReplyMessageImpl message(0, response_to);
    message.numberReturned(1);
    message.documents().push_back(Bson::DocumentImpl::create()->addString("hello", "world"));
    encoder.encodeReply(message);
    return output.toString();
  }

  // Checks a stat's flat name, the name it is tag-extracted to, and the tags attached to it.
  void expectStat(const Stats::Metric* metric, const std::string& name,
                  const std::string& tag_extracted_name,
                  const std::vector<std::pair<std::string, std::string>>& tags) {
    ASSERT_NE(metric, nullptr) << "no stat named '" << name << "'";
    EXPECT_EQ(metric->tagExtractedName(), tag_extracted_name) << " for stat '" << name << "'";

    std::vector<std::pair<std::string, std::string>> actual_tags;
    for (const Stats::Tag& tag : metric->tags()) {
      actual_tags.emplace_back(tag.name_, tag.value_);
    }
    std::sort(actual_tags.begin(), actual_tags.end());
    std::vector<std::pair<std::string, std::string>> expected_tags = tags;
    std::sort(expected_tags.begin(), expected_tags.end());
    EXPECT_EQ(actual_tags, expected_tags) << " for stat '" << name << "'";
  }

  void expectCounter(const std::string& name, const std::string& tag_extracted_name,
                     const std::vector<std::pair<std::string, std::string>>& tags) {
    expectStat(test_server_->counter(name).get(), name, tag_extracted_name, tags);
  }

  void expectGauge(const std::string& name, const std::string& tag_extracted_name,
                   const std::vector<std::pair<std::string, std::string>>& tags) {
    expectStat(test_server_->gauge(name).get(), name, tag_extracted_name, tags);
  }

  void expectHistogram(const std::string& name, const std::string& tag_extracted_name,
                       const std::vector<std::pair<std::string, std::string>>& tags) {
    expectStat(test_server_->histogram(name).get(), name, tag_extracted_name, tags);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MongoProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// The mongo stats have the same flat names, tag-extracted names and tags whether the tags are
// supplied explicitly (IPv4) or derived by the legacy tag-extraction rules (IPv6).
TEST_P(MongoProxyIntegrationTest, StatsTagsAndNamesAreIdenticalInBothModes) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // A plain query with a callsite comment, answered by the upstream. Replies are forwarded
  // downstream only after the filter has fully processed them, so waiting for the reply bytes on
  // the client guarantees all the reply stats (including the histograms) have been created.
  const std::string query = encodeQuery(
      1, "db.testcol",
      Bson::DocumentImpl::create()->addString("$comment", R"({"callingFunction":"getByMongoId"})"));
  const std::string reply1 = encodeReply(1);
  ASSERT_TRUE(tcp_client->write(query));
  ASSERT_TRUE(fake_upstream_connection->waitForData(query.size()));
  ASSERT_TRUE(fake_upstream_connection->write(reply1));
  ASSERT_TRUE(tcp_client->waitForData(reply1.size()));

  // An "insert" command, answered by the upstream.
  const std::string command =
      encodeQuery(2, "db.$cmd", Bson::DocumentImpl::create()->addString("insert", "testcol"));
  const std::string reply2 = encodeReply(2);
  ASSERT_TRUE(tcp_client->write(command));
  ASSERT_TRUE(fake_upstream_connection->waitForData(query.size() + command.size()));
  ASSERT_TRUE(fake_upstream_connection->write(reply2));
  ASSERT_TRUE(tcp_client->waitForData(reply1.size() + reply2.size()));

  tcp_client->close();

  const auto& tag_names = Config::TagNames::get();
  const std::pair<std::string, std::string> prefix{tag_names.MONGO_PREFIX, "mongo_stats"};
  const std::pair<std::string, std::string> collection{tag_names.MONGO_COLLECTION, "testcol"};
  const std::pair<std::string, std::string> callsite{tag_names.MONGO_CALLSITE, "getByMongoId"};
  const std::pair<std::string, std::string> cmd{tag_names.MONGO_CMD, "insert"};

  expectCounter("mongo.mongo_stats.op_query", "mongo.op_query", {prefix});
  expectCounter("mongo.mongo_stats.op_reply", "mongo.op_reply", {prefix});
  expectGauge("mongo.mongo_stats.op_query_active", "mongo.op_query_active", {prefix});
  expectCounter("mongo.mongo_stats.collection.testcol.query.total", "mongo.collection.query.total",
                {prefix, collection});
  expectCounter("mongo.mongo_stats.collection.testcol.callsite.getByMongoId.query.total",
                "mongo.collection.callsite.query.total", {prefix, collection, callsite});
  expectCounter("mongo.mongo_stats.cmd.insert.total", "mongo.cmd.total", {prefix, cmd});
  expectHistogram("mongo.mongo_stats.collection.testcol.query.reply_time_ms",
                  "mongo.collection.query.reply_time_ms", {prefix, collection});
  expectHistogram("mongo.mongo_stats.cmd.insert.reply_time_ms", "mongo.cmd.reply_time_ms",
                  {prefix, cmd});
}

// A stat prefix containing ':' keeps its spelling in the flat stat names in both modes, like the
// historical stat creation paths: the scope is created from StatNames, which are never sanitized.
TEST_P(MongoProxyIntegrationTest, ColonInStatPrefixIsPreserved) {
  initializeWithPrefix("db:27017");

  // The fixed proxy stats are created at config load, so no traffic is needed.
  expectCounter("mongo.db:27017.op_query", "mongo.op_query",
                {{Config::TagNames::get().MONGO_PREFIX, "db:27017"}});
}

} // namespace
} // namespace Envoy
