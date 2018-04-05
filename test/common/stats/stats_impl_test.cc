#include <algorithm>
#include <chrono>
#include <string>

#include "envoy/config/metrics/v2/stats.pb.h"
#include "envoy/stats/stats_macros.h"

#include "common/config/well_known_names.h"
#include "common/stats/stats_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

TEST(StatsIsolatedStoreImplTest, All) {
  IsolatedStoreImpl store;

  ScopePtr scope1 = store.createScope("scope1.");
  Counter& c1 = store.counter("c1");
  Counter& c2 = scope1->counter("c2");
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());
  EXPECT_EQ("c1", c1.tagExtractedName());
  EXPECT_EQ("scope1.c2", c2.tagExtractedName());
  EXPECT_EQ(0, c1.tags().size());
  EXPECT_EQ(0, c1.tags().size());

  Gauge& g1 = store.gauge("g1");
  Gauge& g2 = scope1->gauge("g2");
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());
  EXPECT_EQ("g1", g1.tagExtractedName());
  EXPECT_EQ("scope1.g2", g2.tagExtractedName());
  EXPECT_EQ(0, g1.tags().size());
  EXPECT_EQ(0, g1.tags().size());

  Histogram& h1 = store.histogram("h1");
  Histogram& h2 = scope1->histogram("h2");
  scope1->deliverHistogramToSinks(h2, 0);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  EXPECT_EQ("h1", h1.tagExtractedName());
  EXPECT_EQ("scope1.h2", h2.tagExtractedName());
  EXPECT_EQ(0, h1.tags().size());
  EXPECT_EQ(0, h2.tags().size());
  h1.recordValue(200);
  h2.recordValue(200);

  ScopePtr scope2 = scope1->createScope("foo.");
  EXPECT_EQ("scope1.foo.bar", scope2->counter("bar").name());

  EXPECT_EQ(3UL, store.counters().size());
  EXPECT_EQ(2UL, store.gauges().size());
}

/**
 * Test stats macros. @see stats_macros.h
 */
// clang-format off
#define ALL_TEST_STATS(COUNTER, GAUGE, HISTOGRAM)                                                  \
  COUNTER  (test_counter)                                                                          \
  GAUGE    (test_gauge)                                                                            \
  HISTOGRAM(test_histogram)
// clang-format on

struct TestStats {
  ALL_TEST_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

TEST(StatsMacros, All) {
  IsolatedStoreImpl stats_store;
  TestStats test_stats{ALL_TEST_STATS(POOL_COUNTER_PREFIX(stats_store, "test."),
                                      POOL_GAUGE_PREFIX(stats_store, "test."),
                                      POOL_HISTOGRAM_PREFIX(stats_store, "test."))};

  Counter& counter = test_stats.test_counter_;
  EXPECT_EQ("test.test_counter", counter.name());

  Gauge& gauge = test_stats.test_gauge_;
  EXPECT_EQ("test.test_gauge", gauge.name());

  Histogram& histogram = test_stats.test_histogram_;
  EXPECT_EQ("test.test_histogram", histogram.name());
}

TEST(TagExtractorTest, TwoSubexpressions) {
  TagExtractorImpl tag_extractor("cluster_name", "^cluster\\.((.+?)\\.)");
  EXPECT_EQ("cluster_name", tag_extractor.name());
  std::string name = "cluster.test_cluster.upstream_cx_total";
  std::vector<Tag> tags;
  IntervalSetImpl<size_t> remove_characters;
  ASSERT_TRUE(tag_extractor.extractTag(name, tags, remove_characters));
  std::string tag_extracted_name = StringUtil::removeCharacters(name, remove_characters);
  EXPECT_EQ("cluster.upstream_cx_total", tag_extracted_name);
  ASSERT_EQ(1, tags.size());
  EXPECT_EQ("test_cluster", tags.at(0).value_);
  EXPECT_EQ("cluster_name", tags.at(0).name_);
}

TEST(TagExtractorTest, SingleSubexpression) {
  TagExtractorImpl tag_extractor("listner_port", "^listener\\.(\\d+?\\.)");
  std::string name = "listener.80.downstream_cx_total";
  std::vector<Tag> tags;
  IntervalSetImpl<size_t> remove_characters;
  ASSERT_TRUE(tag_extractor.extractTag(name, tags, remove_characters));
  std::string tag_extracted_name = StringUtil::removeCharacters(name, remove_characters);
  EXPECT_EQ("listener.downstream_cx_total", tag_extracted_name);
  ASSERT_EQ(1, tags.size());
  EXPECT_EQ("80.", tags.at(0).value_);
  EXPECT_EQ("listner_port", tags.at(0).name_);
}

TEST(TagExtractorTest, substrMismatch) {
  TagExtractorImpl tag_extractor("listner_port", "^listener\\.(\\d+?\\.)\\.foo\\.", ".foo.");
  EXPECT_TRUE(tag_extractor.substrMismatch("listener.80.downstream_cx_total"));
  EXPECT_FALSE(tag_extractor.substrMismatch("listener.80.downstream_cx_total.foo.bar"));
}

TEST(TagExtractorTest, noSubstrMismatch) {
  TagExtractorImpl tag_extractor("listner_port", "^listener\\.(\\d+?\\.)\\.foo\\.");
  EXPECT_FALSE(tag_extractor.substrMismatch("listener.80.downstream_cx_total"));
  EXPECT_FALSE(tag_extractor.substrMismatch("listener.80.downstream_cx_total.foo.bar"));
}

TEST(TagExtractorTest, EmptyName) {
  EXPECT_THROW_WITH_MESSAGE(TagExtractorImpl::createTagExtractor("", "^listener\\.(\\d+?\\.)"),
                            EnvoyException, "tag_name cannot be empty");
}

TEST(TagExtractorTest, BadRegex) {
  EXPECT_THROW_WITH_REGEX(TagExtractorImpl::createTagExtractor("cluster_name", "+invalid"),
                          EnvoyException, "Invalid regex '\\+invalid':");
}

class DefaultTagRegexTester {
public:
  DefaultTagRegexTester() : tag_extractors_(envoy::config::metrics::v2::StatsConfig()) {}

  void testRegex(const std::string& stat_name, const std::string& expected_tag_extracted_name,
                 const std::vector<Tag>& expected_tags) {

    // Test forward iteration through the regexes
    std::vector<Tag> tags;
    const std::string tag_extracted_name = tag_extractors_.produceTags(stat_name, tags);

    auto cmp = [](const Tag& lhs, const Tag& rhs) {
      return lhs.name_ == rhs.name_ && lhs.value_ == rhs.value_;
    };

    EXPECT_EQ(expected_tag_extracted_name, tag_extracted_name);
    ASSERT_EQ(expected_tags.size(), tags.size())
        << fmt::format("Stat name '{}' did not produce the expected number of tags", stat_name);
    EXPECT_TRUE(std::is_permutation(expected_tags.begin(), expected_tags.end(), tags.begin(), cmp))
        << fmt::format("Stat name '{}' did not produce the expected tags", stat_name);

    // Reverse iteration through regexes to ensure ordering invariance
    std::vector<Tag> rev_tags;
    const std::string rev_tag_extracted_name = produceTagsReverse(stat_name, rev_tags);

    EXPECT_EQ(expected_tag_extracted_name, rev_tag_extracted_name);
    ASSERT_EQ(expected_tags.size(), rev_tags.size())
        << fmt::format("Stat name '{}' did not produce the expected number of tags when regexes "
                       "were run in reverse order",
                       stat_name);
    EXPECT_TRUE(
        std::is_permutation(expected_tags.begin(), expected_tags.end(), rev_tags.begin(), cmp))
        << fmt::format("Stat name '{}' did not produce the expected tags when regexes were run in "
                       "reverse order",
                       stat_name);
  }

  /**
   * Reimplements TagProducerImpl::produceTags, but extracts the tags in reverse order.
   * This helps demonstrate that the order of extractors does not matter to the end result,
   * assuming we don't care about tag-order. This is in large part correct by design because
   * stat_name is not mutated until all the extraction is done.
   * @param metric_name std::string a name of Stats::Metric (Counter, Gauge, Histogram).
   * @param tags std::vector<Tag>& a set of Stats::Tag.
   * @return std::string the metric_name with tags removed.
   */
  std::string produceTagsReverse(const std::string& metric_name, std::vector<Tag>& tags) const {
    // Note: one discrepency between this and TagProducerImpl::produceTags is that this
    // version does not add in tag_extractors_.default_tags_ into tags. That doesn't matter
    // for this test, however.
    std::list<const TagExtractor*> extractors; // Note push-front is used to reverse order.
    tag_extractors_.forEachExtractorMatching(metric_name,
                                             [&extractors](const TagExtractorPtr& tag_extractor) {
                                               extractors.push_front(tag_extractor.get());
                                             });

    IntervalSetImpl<size_t> remove_characters;
    for (const TagExtractor* tag_extractor : extractors) {
      tag_extractor->extractTag(metric_name, tags, remove_characters);
    }
    return StringUtil::removeCharacters(metric_name, remove_characters);
  }

  TagProducerImpl tag_extractors_;
};

TEST(TagExtractorTest, DefaultTagExtractors) {
  const auto& tag_names = Config::TagNames::get();

  // General cluster name
  DefaultTagRegexTester regex_tester;

  // Cluster name
  Tag cluster_tag;
  cluster_tag.name_ = tag_names.CLUSTER_NAME;
  cluster_tag.value_ = "ratelimit";

  regex_tester.testRegex("cluster.ratelimit.upstream_rq_timeout", "cluster.upstream_rq_timeout",
                         {cluster_tag});

  // Listener SSL
  Tag listener_address;
  listener_address.name_ = tag_names.LISTENER_ADDRESS;

  // ipv6 loopback address
  listener_address.value_ = "[__1]_0";

  // Cipher
  Tag cipher_name;
  cipher_name.name_ = tag_names.SSL_CIPHER;
  cipher_name.value_ = "AES256-SHA";

  regex_tester.testRegex("listener.[__1]_0.ssl.cipher.AES256-SHA", "listener.ssl.cipher",
                         {listener_address, cipher_name});

  // Cipher suite
  Tag cipher_suite;
  cipher_suite.name_ = tag_names.SSL_CIPHER_SUITE;
  cipher_suite.value_ = "ECDHE-RSA-AES128-GCM-SHA256";

  regex_tester.testRegex("cluster.ratelimit.ssl.ciphers.ECDHE-RSA-AES128-GCM-SHA256",
                         "cluster.ssl.ciphers", {cluster_tag, cipher_suite});

  // ipv6 non-loopback (for alphabetical chars)
  listener_address.value_ = "[2001_0db8_85a3_0000_0000_8a2e_0370_7334]_3543";
  regex_tester.testRegex(
      "listener.[2001_0db8_85a3_0000_0000_8a2e_0370_7334]_3543.ssl.cipher.AES256-SHA",
      "listener.ssl.cipher", {listener_address, cipher_name});

  // ipv4 address
  listener_address.value_ = "127.0.0.1_0";
  regex_tester.testRegex("listener.127.0.0.1_0.ssl.cipher.AES256-SHA", "listener.ssl.cipher",
                         {listener_address, cipher_name});

  // Mongo
  Tag mongo_prefix;
  mongo_prefix.name_ = tag_names.MONGO_PREFIX;
  mongo_prefix.value_ = "mongo_filter";

  Tag mongo_command;
  mongo_command.name_ = tag_names.MONGO_CMD;
  mongo_command.value_ = "foo_cmd";

  Tag mongo_collection;
  mongo_collection.name_ = tag_names.MONGO_COLLECTION;
  mongo_collection.value_ = "bar_collection";

  Tag mongo_callsite;
  mongo_callsite.name_ = tag_names.MONGO_CALLSITE;
  mongo_callsite.value_ = "baz_callsite";

  regex_tester.testRegex("mongo.mongo_filter.op_reply", "mongo.op_reply", {mongo_prefix});
  regex_tester.testRegex("mongo.mongo_filter.cmd.foo_cmd.reply_size", "mongo.cmd.reply_size",
                         {mongo_prefix, mongo_command});
  regex_tester.testRegex("mongo.mongo_filter.collection.bar_collection.query.multi_get",
                         "mongo.collection.query.multi_get", {mongo_prefix, mongo_collection});
  regex_tester.testRegex(
      "mongo.mongo_filter.collection.bar_collection.callsite.baz_callsite.query.scatter_get",
      "mongo.collection.callsite.query.scatter_get",
      {mongo_prefix, mongo_collection, mongo_callsite});

  // Ratelimit
  Tag ratelimit_prefix;
  ratelimit_prefix.name_ = tag_names.RATELIMIT_PREFIX;
  ratelimit_prefix.value_ = "foo_ratelimiter";
  regex_tester.testRegex("ratelimit.foo_ratelimiter.over_limit", "ratelimit.over_limit",
                         {ratelimit_prefix});

  // Dynamo
  Tag dynamo_http_prefix;
  dynamo_http_prefix.name_ = tag_names.HTTP_CONN_MANAGER_PREFIX;
  dynamo_http_prefix.value_ = "egress_dynamodb_iad";

  Tag dynamo_operation;
  dynamo_operation.name_ = tag_names.DYNAMO_OPERATION;
  dynamo_operation.value_ = "Query";

  Tag dynamo_table;
  dynamo_table.name_ = tag_names.DYNAMO_TABLE;
  dynamo_table.value_ = "bar_table";

  Tag dynamo_partition;
  dynamo_partition.name_ = tag_names.DYNAMO_PARTITION_ID;
  dynamo_partition.value_ = "ABC1234";

  regex_tester.testRegex("http.egress_dynamodb_iad.downstream_cx_total", "http.downstream_cx_total",
                         {dynamo_http_prefix});
  regex_tester.testRegex("http.egress_dynamodb_iad.dynamodb.operation.Query.upstream_rq_time",
                         "http.dynamodb.operation.upstream_rq_time",
                         {dynamo_http_prefix, dynamo_operation});
  regex_tester.testRegex("http.egress_dynamodb_iad.dynamodb.table.bar_table.upstream_rq_time",
                         "http.dynamodb.table.upstream_rq_time",
                         {dynamo_http_prefix, dynamo_table});
  regex_tester.testRegex(
      "http.egress_dynamodb_iad.dynamodb.table.bar_table.capacity.Query.__partition_id=ABC1234",
      "http.dynamodb.table.capacity",
      {dynamo_http_prefix, dynamo_table, dynamo_operation, dynamo_partition});

  // GRPC Http1.1 Bridge
  Tag grpc_cluster;
  grpc_cluster.name_ = tag_names.CLUSTER_NAME;
  grpc_cluster.value_ = "grpc_cluster";

  Tag grpc_service;
  grpc_service.name_ = tag_names.GRPC_BRIDGE_SERVICE;
  grpc_service.value_ = "grpc_service_1";

  Tag grpc_method;
  grpc_method.name_ = tag_names.GRPC_BRIDGE_METHOD;
  grpc_method.value_ = "grpc_method_1";

  regex_tester.testRegex("cluster.grpc_cluster.grpc.grpc_service_1.grpc_method_1.success",
                         "cluster.grpc.success", {grpc_cluster, grpc_method, grpc_service});

  // Virtual host and cluster
  Tag vhost;
  vhost.name_ = tag_names.VIRTUAL_HOST;
  vhost.value_ = "vhost_1";

  Tag vcluster;
  vcluster.name_ = tag_names.VIRTUAL_CLUSTER;
  vcluster.value_ = "vcluster_1";

  Tag response_code_class;
  response_code_class.name_ = tag_names.RESPONSE_CODE_CLASS;
  response_code_class.value_ = "2";

  Tag response_code;
  response_code.name_ = tag_names.RESPONSE_CODE;
  response_code.value_ = "200";

  regex_tester.testRegex("vhost.vhost_1.vcluster.vcluster_1.upstream_rq_2xx",
                         "vhost.vcluster.upstream_rq_xx", {vhost, vcluster, response_code_class});
  regex_tester.testRegex("vhost.vhost_1.vcluster.vcluster_1.upstream_rq_200",
                         "vhost.vcluster.upstream_rq", {vhost, vcluster, response_code});

  // Listener http prefix
  Tag listener_http_prefix;
  listener_http_prefix.name_ = tag_names.HTTP_CONN_MANAGER_PREFIX;
  listener_http_prefix.value_ = "http_prefix";

  listener_address.value_ = "127.0.0.1_3012";
  response_code_class.value_ = "5";

  regex_tester.testRegex("listener.127.0.0.1_3012.http.http_prefix.downstream_rq_5xx",
                         "listener.http.downstream_rq_xx",
                         {listener_http_prefix, listener_address, response_code_class});

  // User agent
  Tag user_agent;
  user_agent.name_ = tag_names.HTTP_USER_AGENT;
  user_agent.value_ = "ios";

  regex_tester.testRegex("http.egress_dynamodb_iad.user_agent.ios.downstream_cx_total",
                         "http.user_agent.downstream_cx_total", {user_agent, dynamo_http_prefix});

  // Client SSL Prefix
  Tag client_ssl;
  client_ssl.name_ = tag_names.CLIENTSSL_PREFIX;
  client_ssl.value_ = "clientssl_prefix";

  regex_tester.testRegex("auth.clientssl.clientssl_prefix.auth_ip_white_list",
                         "auth.clientssl.auth_ip_white_list", {client_ssl});

  // TCP Prefix
  Tag tcp_prefix;
  tcp_prefix.name_ = tag_names.TCP_PREFIX;
  tcp_prefix.value_ = "tcp_prefix";

  regex_tester.testRegex("tcp.tcp_prefix.downstream_flow_control_resumed_reading_total",
                         "tcp.downstream_flow_control_resumed_reading_total", {tcp_prefix});

  // Fault Downstream Cluster
  Tag fault_connection_manager;
  fault_connection_manager.name_ = tag_names.HTTP_CONN_MANAGER_PREFIX;
  fault_connection_manager.value_ = "fault_connection_manager";

  Tag fault_downstream_cluster;
  fault_downstream_cluster.name_ = tag_names.FAULT_DOWNSTREAM_CLUSTER;
  fault_downstream_cluster.value_ = "fault_cluster";

  regex_tester.testRegex("http.fault_connection_manager.fault.fault_cluster.aborts_injected",
                         "http.fault.aborts_injected",
                         {fault_connection_manager, fault_downstream_cluster});
}

TEST(TagExtractorTest, ExtractRegexPrefix) {
  TagExtractorPtr tag_extractor; // Keep tag_extractor in this scope to prolong prefix lifetime.
  auto extractRegexPrefix = [&tag_extractor](const std::string& regex) -> absl::string_view {
    tag_extractor = TagExtractorImpl::createTagExtractor("foo", regex);
    return tag_extractor->prefixToken();
  };

  EXPECT_EQ("", extractRegexPrefix("^prefix(foo)."));
  EXPECT_EQ("prefix", extractRegexPrefix("^prefix\\.foo"));
  EXPECT_EQ("prefix_optional", extractRegexPrefix("^prefix_optional(?=\\.)"));
  EXPECT_EQ("", extractRegexPrefix("^notACompleteToken"));   //
  EXPECT_EQ("onlyToken", extractRegexPrefix("^onlyToken$")); //
  EXPECT_EQ("", extractRegexPrefix("(prefix)"));
  EXPECT_EQ("", extractRegexPrefix("^(prefix)"));
  EXPECT_EQ("", extractRegexPrefix("prefix(foo)"));
}

TEST(TagExtractorTest, CreateTagExtractorNoRegex) {
  EXPECT_THROW_WITH_REGEX(TagExtractorImpl::createTagExtractor("no such default tag", ""),
                          EnvoyException, "^No regex specified for tag specifier and no default");
}

TEST(TagProducerTest, CheckConstructor) {
  envoy::config::metrics::v2::StatsConfig stats_config;

  // Should pass there were no tag name conflict.
  auto& tag_specifier1 = *stats_config.mutable_stats_tags()->Add();
  tag_specifier1.set_tag_name("test.x");
  tag_specifier1.set_fixed_value("xxx");
  TagProducerImpl{stats_config};

  // Should raise an error when duplicate tag names are specified.
  auto& tag_specifier2 = *stats_config.mutable_stats_tags()->Add();
  tag_specifier2.set_tag_name("test.x");
  tag_specifier2.set_fixed_value("yyy");
  EXPECT_THROW_WITH_MESSAGE(TagProducerImpl{stats_config}, EnvoyException,
                            fmt::format("Tag name '{}' specified twice.", "test.x"));

  // Also should raise an error when user defined tag name conflicts with Envoy's default tag names.
  stats_config.clear_stats_tags();
  stats_config.mutable_use_all_default_tags()->set_value(true);
  auto& custom_tag_extractor = *stats_config.mutable_stats_tags()->Add();
  custom_tag_extractor.set_tag_name(Config::TagNames::get().CLUSTER_NAME);
  EXPECT_THROW_WITH_MESSAGE(
      TagProducerImpl{stats_config}, EnvoyException,
      fmt::format("Tag name '{}' specified twice.", Config::TagNames::get().CLUSTER_NAME));

  // Non-default custom name without regex should throw
  stats_config.mutable_use_all_default_tags()->set_value(true);
  stats_config.clear_stats_tags();
  custom_tag_extractor = *stats_config.mutable_stats_tags()->Add();
  custom_tag_extractor.set_tag_name("test_extractor");
  EXPECT_THROW_WITH_MESSAGE(
      TagProducerImpl{stats_config}, EnvoyException,
      "No regex specified for tag specifier and no default regex for name: 'test_extractor'");

  // Also empty regex should throw
  stats_config.mutable_use_all_default_tags()->set_value(true);
  stats_config.clear_stats_tags();
  custom_tag_extractor = *stats_config.mutable_stats_tags()->Add();
  custom_tag_extractor.set_tag_name("test_extractor");
  custom_tag_extractor.set_regex("");
  EXPECT_THROW_WITH_MESSAGE(
      TagProducerImpl{stats_config}, EnvoyException,
      "No regex specified for tag specifier and no default regex for name: 'test_extractor'");
}

} // namespace Stats
} // namespace Envoy
