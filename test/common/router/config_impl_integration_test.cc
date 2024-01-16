#include <chrono>
#include <cstdint>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"

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

    RouteConstSharedPtr route(RouteConstSharedPtr parent,
                              const Http::RequestHeaderMap&) const override {
      ASSERT(dynamic_cast<const RouteEntryImplBase*>(parent.get()) != nullptr);
      return std::make_shared<RouteEntryImplBase::DynamicRouteEntry>(
          dynamic_cast<const RouteEntryImplBase*>(parent.get()), parent, cluster_name_);
    }

    const std::string cluster_name_;
  };

  FakeClusterSpecifierPluginFactoryConfig() = default;
  ClusterSpecifierPluginSharedPtr
  createClusterSpecifierPlugin(const Protobuf::Message& config,
                               Server::Configuration::CommonFactoryContext&) override {
    const auto& typed_config = dynamic_cast<const ProtobufWkt::Struct&>(config);
    return std::make_shared<FakeClusterSpecifierPlugin>(
        typed_config.fields().at("name").string_value());
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
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

} // namespace
} // namespace Router
} // namespace Envoy
