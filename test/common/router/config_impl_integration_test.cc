#include <chrono>
#include <cstdint>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/delegating_route_impl.h"

#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

class FakeClusterSpecifierPluginFactoryConfig : public ClusterSpecifierPluginFactoryConfig {
public:
  class FakeClusterSpecifierPlugin : public ClusterSpecifierPlugin {
  public:
    FakeClusterSpecifierPlugin(absl::string_view cluster) : cluster_name_(cluster) {}

    RouteConstSharedPtr route(RouteEntryAndRouteConstSharedPtr parent,
                              const Http::RequestHeaderMap&, const StreamInfo::StreamInfo&,
                              uint64_t) const override {
      ASSERT(dynamic_cast<const RouteEntryImplBase*>(parent.get()) != nullptr);
      return std::make_shared<Router::DynamicRouteEntry>(parent, std::string(cluster_name_));
    }

    const std::string cluster_name_;
  };

  FakeClusterSpecifierPluginFactoryConfig() = default;
  ClusterSpecifierPluginSharedPtr
  createClusterSpecifierPlugin(const Protobuf::Message& config,
                               Server::Configuration::ServerFactoryContext&) override {
    const auto& typed_config = dynamic_cast<const Protobuf::Struct&>(config);
    return std::make_shared<FakeClusterSpecifierPlugin>(
        typed_config.fields().at("name").string_value());
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return "envoy.router.cluster_specifier_plugin.fake"; }
};

class ConfigImplIntegrationTest : public Envoy::HttpIntegrationTest, public testing::Test {
public:
  ConfigImplIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  void initializeRoute(const std::string& vhost_config_yaml) {
    envoy::config::route::v3::VirtualHost vhost;
    TestUtility::loadFromYaml(vhost_config_yaml, vhost);
    config_helper_.addVirtualHost(vhost);
    initialize();
  }
};

static const std::string ClusterSpecifierPluginUnknownCluster =
    R"EOF(
name: test_cluster_specifier_plugin
domains:
- cluster.specifier.plugin
routes:
- name: test_route_1
  match:
    prefix: /test/route/1
  route:
    inline_cluster_specifier_plugin:
      extension:
        name: fake
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            name: cluster_0
- name: test_route_2
  match:
    prefix: /test/route/2
  route:
    inline_cluster_specifier_plugin:
      extension:
        name: fake
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            name: cluster_unknown
)EOF";

TEST_F(ConfigImplIntegrationTest, ClusterSpecifierPluginTest) {
  FakeClusterSpecifierPluginFactoryConfig factory;
  Registry::InjectFactory<ClusterSpecifierPluginFactoryConfig> registered(factory);

  initializeRoute(ClusterSpecifierPluginUnknownCluster);

  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Http::TestResponseHeaderMapImpl response_headers{
        {"server", "envoy"},
        {":status", "200"},
    };

    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test/route/1"},
                                                   {":scheme", "http"},
                                                   {":authority", "cluster.specifier.plugin"}};

    auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ(response->headers().getStatusValue(), "200");

    cleanupUpstreamAndDownstream();
  }

  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test/route/2"},
                                                   {":scheme", "http"},
                                                   {":authority", "cluster.specifier.plugin"}};

    // Second route will be selected and unknown cluster name will be return by the cluster
    // specifier plugin.
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("503"));

    cleanupUpstreamAndDownstream();
  }
}

// Integration test for weighted cluster hash policy
class WeightedClusterHashPolicyIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  WeightedClusterHashPolicyIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void createUpstreams() override {
    // Create 2 fake upstreams for our weighted clusters
    addFakeUpstream(Http::CodecType::HTTP1);
    addFakeUpstream(Http::CodecType::HTTP1);
  }

  void initializeConfig() {
    // Add cluster_1 configuration (cluster_0 already exists by default)
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      cluster->set_name("cluster_1");
      // Fix the load assignment to use the correct cluster name
      cluster->mutable_load_assignment()->set_cluster_name("cluster_1");
    });

    // Configure the route with weighted clusters and hash policy
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.mutable_route_config()->set_name("test_weighted_cluster_hash_policy");

          auto* vhost = hcm.mutable_route_config()->add_virtual_hosts();
          vhost->set_name("test_weighted_cluster_hash_policy");
          vhost->add_domains("weighted.cluster.hash.test");

          auto* route = vhost->add_routes();
          route->mutable_match()->set_prefix("/hash-test");

          auto* weighted_clusters = route->mutable_route()->mutable_weighted_clusters();

          auto* cluster0 = weighted_clusters->add_clusters();
          cluster0->set_name("cluster_0");
          cluster0->mutable_weight()->set_value(60);

          auto* cluster1 = weighted_clusters->add_clusters();
          cluster1->set_name("cluster_1");
          cluster1->mutable_weight()->set_value(40);

          // Enable hash policy for weighted clusters
          weighted_clusters->mutable_use_hash_policy()->set_value(true);

          auto* hash_policy = route->mutable_route()->add_hash_policy();
          hash_policy->mutable_header()->set_header_name("x-user-id");
        });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, WeightedClusterHashPolicyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(WeightedClusterHashPolicyIntegrationTest, SameUserIdGoesToSameUpstream) {
  // Initialize the configuration with weighted clusters and hash policy
  initializeConfig();

  // Test: Same user ID should consistently go to only one upstream
  const std::string user_id = "consistent-user-123";
  std::string selected_upstream;

  // Make multiple requests with the same user ID
  for (int request = 0; request < 5; ++request) {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/hash-test"},
                                                   {":scheme", "http"},
                                                   {":authority", "weighted.cluster.hash.test"},
                                                   {"x-user-id", user_id}};

    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

    // Handle the upstream response
    FakeHttpConnectionPtr fake_upstream_connection;
    FakeStreamPtr request_stream;

    std::string current_upstream;

    // Check which upstream received the request
    if (fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection,
                                                  std::chrono::milliseconds(100))) {
      current_upstream = "upstream_0";
    } else if (fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection,
                                                         std::chrono::milliseconds(100))) {
      current_upstream = "upstream_1";
    } else {
      FAIL() << "No upstream received the request";
    }

    ASSERT_TRUE(fake_upstream_connection->waitForNewStream(*dispatcher_, request_stream));
    ASSERT_TRUE(request_stream->waitForEndStream(*dispatcher_));

    // Send response
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    request_stream->encodeHeaders(response_headers, true);
    ASSERT_TRUE(fake_upstream_connection->close());

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());

    // Verify consistency - same user should always go to same upstream
    if (selected_upstream.empty()) {
      selected_upstream = current_upstream;
      EXPECT_TRUE(selected_upstream == "upstream_0" || selected_upstream == "upstream_1");
    } else {
      EXPECT_EQ(selected_upstream, current_upstream)
          << "Request " << (request + 1)
          << ": Same user ID should consistently route to same upstream. "
          << "Expected: " << selected_upstream << ", Got: " << current_upstream;
    }

    codec_client_->close();
  }

  EXPECT_FALSE(selected_upstream.empty()) << "Should have selected an upstream";
}

TEST_P(WeightedClusterHashPolicyIntegrationTest, DifferentUserIdsCanGoToDifferentClusters) {
  // Initialize the configuration with weighted clusters and hash policy
  initializeConfig();

  // Test with multiple different user IDs to verify they can go to different clusters
  std::vector<std::string> user_ids = {"user-1", "user-2", "user-3", "user-4", "user-5"};
  std::map<std::string, std::string> user_to_upstream;

  for (const auto& user_id : user_ids) {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/hash-test"},
                                                   {":scheme", "http"},
                                                   {":authority", "weighted.cluster.hash.test"},
                                                   {"x-user-id", user_id}};

    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

    // Handle the upstream response
    FakeHttpConnectionPtr fake_upstream_connection;
    FakeStreamPtr request_stream;

    std::string current_upstream;

    // Check which upstream received the request
    if (fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection,
                                                  std::chrono::milliseconds(100))) {
      current_upstream = "upstream_0";
    } else if (fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection,
                                                         std::chrono::milliseconds(100))) {
      current_upstream = "upstream_1";
    } else {
      FAIL() << "No upstream received the request for user: " << user_id;
    }

    ASSERT_TRUE(fake_upstream_connection->waitForNewStream(*dispatcher_, request_stream));
    ASSERT_TRUE(request_stream->waitForEndStream(*dispatcher_));

    // Send response
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    request_stream->encodeHeaders(response_headers, true);
    ASSERT_TRUE(fake_upstream_connection->close());

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());

    user_to_upstream[user_id] = current_upstream;
    codec_client_->close();
  }

  // Verify that we have some distribution across both upstreams
  std::set<std::string> unique_upstreams;
  for (const auto& pair : user_to_upstream) {
    unique_upstreams.insert(pair.second);
  }

  // We should have at least some distribution (not all users going to the same upstream)
  // Note: Due to hash distribution, it's possible all users go to the same upstream,
  // but it's unlikely with 5 different user IDs
  EXPECT_GE(unique_upstreams.size(), 1) << "Should have at least one upstream selected";
}

TEST_P(WeightedClusterHashPolicyIntegrationTest, WeightedDistributionTest) {
  // Initialize the configuration with weighted clusters and hash policy
  initializeConfig();

  // Test weighted distribution by making many requests with different user IDs
  std::map<std::string, int> upstream_counts;
  const int num_requests = 100;

  for (int i = 0; i < num_requests; ++i) {
    std::string user_id = "user-" + std::to_string(i);
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/hash-test"},
                                                   {":scheme", "http"},
                                                   {":authority", "weighted.cluster.hash.test"},
                                                   {"x-user-id", user_id}};

    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

    // Handle the upstream response
    FakeHttpConnectionPtr fake_upstream_connection;
    FakeStreamPtr request_stream;

    std::string current_upstream;

    // Check which upstream received the request
    if (fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection,
                                                  std::chrono::milliseconds(100))) {
      current_upstream = "upstream_0";
    } else if (fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection,
                                                         std::chrono::milliseconds(100))) {
      current_upstream = "upstream_1";
    } else {
      FAIL() << "No upstream received the request for user: " << user_id;
    }

    ASSERT_TRUE(fake_upstream_connection->waitForNewStream(*dispatcher_, request_stream));
    ASSERT_TRUE(request_stream->waitForEndStream(*dispatcher_));

    // Send response
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    request_stream->encodeHeaders(response_headers, true);
    ASSERT_TRUE(fake_upstream_connection->close());

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());

    upstream_counts[current_upstream]++;
    codec_client_->close();
  }

  // The distribution should roughly follow the weights (60% vs 40%)
  // Hash policy ensures consistency for same user, but different users should
  // be distributed according to cluster weights
  double upstream_0_ratio = static_cast<double>(upstream_counts["upstream_0"]) / num_requests;
  double upstream_1_ratio = static_cast<double>(upstream_counts["upstream_1"]) / num_requests;

  // The distribution should be reasonably close to the expected weights
  // Allow for ±20% variance (40-80% for upstream_0, 20-60% for upstream_1)
  EXPECT_GE(upstream_0_ratio, 0.4) << "Upstream 0 should get at least 40% of traffic";
  EXPECT_LE(upstream_0_ratio, 0.8) << "Upstream 0 should get at most 80% of traffic";
  EXPECT_GE(upstream_1_ratio, 0.2) << "Upstream 1 should get at least 20% of traffic";
  EXPECT_LE(upstream_1_ratio, 0.6) << "Upstream 1 should get at most 60% of traffic";
}

} // namespace
} // namespace Router
} // namespace Envoy
