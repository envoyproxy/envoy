#include <string>
#include <tuple>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/router/route_extension.h"

#include "source/common/common/macros.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/delegating_route_impl.h"

#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using HttpConnectionManager =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;

const Http::LowerCaseString& testHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "x-route-extension");
}

// The route a test extension hands back: everything is delegated to the matched route except
// finalizeRequestHeaders, which appends the extension's tag to a test header before delegating.
// Appending rather than setting lets a test read the order the extensions ran in off the header.
class TagAddingRoute : public DelegatingRouteEntry {
public:
  TagAddingRoute(RouteConstSharedPtr parent, std::string tag)
      : DelegatingRouteEntry(std::move(parent)), tag_(std::move(tag)) {}

  void finalizeRequestHeaders(Http::RequestHeaderMap& headers, const Formatter::Context& context,
                              const StreamInfo::StreamInfo& stream_info,
                              bool insert_envoy_original_path) const override {
    DelegatingRouteEntry::finalizeRequestHeaders(headers, context, stream_info,
                                                 insert_envoy_original_path);
    headers.appendCopy(testHeader(), tag_);
  }

private:
  const std::string tag_;
};

class TagAddingRouteExtension : public RouteExtension {
public:
  explicit TagAddingRouteExtension(std::string tag) : tag_(std::move(tag)) {}

  OnRouteResult onRoute(RouteConstSharedPtr route, const Http::RequestHeaderMap&,
                        const StreamInfo::StreamInfo&, uint64_t) const override {
    // Nothing to decorate: either matching produced no route, or the route is a redirect or direct
    // response, which has no route entry to delegate to.
    if (route == nullptr || route->routeEntry() == nullptr) {
      return {std::move(route)};
    }
    return {std::make_shared<TagAddingRoute>(std::move(route), tag_)};
  }

private:
  const std::string tag_;
};

class TagAddingRouteExtensionFactory : public RouteExtensionFactory {
public:
  absl::StatusOr<RouteExtensionSharedPtr>
  createRouteExtension(const Protobuf::Message& config,
                       Server::Configuration::ServerFactoryContext&) override {
    const auto& typed_config = Envoy::Protobuf::DynamicCastMessage<Protobuf::Struct>(config);
    return std::make_shared<TagAddingRouteExtension>(
        typed_config.fields().at("tag").string_value());
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return "envoy.router.route_extension.add_header"; }
};

envoy::config::core::v3::TypedExtensionConfig extensionConfig(const std::string& tag) {
  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.router.route_extension.add_header");
  Protobuf::Struct typed_config;
  (*typed_config.mutable_fields())["tag"].set_string_value(tag);
  std::ignore = config.mutable_typed_config()->PackFrom(typed_config);
  return config;
}

class RouteExtensionIntegrationTest : public Envoy::HttpIntegrationTest, public testing::Test {
public:
  RouteExtensionIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  // Adds a tagged extension at each of the requested levels of the default route configuration.
  void initializeWithExtensionsAt(bool config_level, bool vhost_level, bool route_level) {
    config_helper_.addConfigModifier(
        [config_level, vhost_level, route_level](HttpConnectionManager& hcm) {
          auto* route_config = hcm.mutable_route_config();
          if (config_level) {
            *route_config->add_route_extensions() = extensionConfig("config");
          }
          auto* vhost = route_config->mutable_virtual_hosts(0);
          if (vhost_level) {
            *vhost->add_route_extensions() = extensionConfig("vhost");
          }
          if (route_level) {
            *vhost->mutable_routes(0)->add_route_extensions() = extensionConfig("route");
          }
        });
    initialize();
  }

  // Sends a request through the proxy and returns the value the extensions left on the test header
  // of the request the upstream saw, or nullopt if they left none.
  std::optional<std::string> upstreamTestHeader() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    EXPECT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());

    const auto header = upstream_request_->headers().get(testHeader());
    if (header.empty()) {
      return std::nullopt;
    }
    return std::string(header[0]->value().getStringView());
  }

  TagAddingRouteExtensionFactory factory_;
  Registry::InjectFactory<RouteExtensionFactory> registered_{factory_};
};

// A route configuration level extension decorates the route of every request.
TEST_F(RouteExtensionIntegrationTest, RouteConfigurationLevelExtension) {
  initializeWithExtensionsAt(true, false, false);
  EXPECT_EQ("config", upstreamTestHeader());
}

TEST_F(RouteExtensionIntegrationTest, VirtualHostLevelExtension) {
  initializeWithExtensionsAt(false, true, false);
  EXPECT_EQ("vhost", upstreamTestHeader());
}

TEST_F(RouteExtensionIntegrationTest, RouteLevelExtension) {
  initializeWithExtensionsAt(false, false, true);
  EXPECT_EQ("route", upstreamTestHeader());
}

// With no extension configured the route is untouched.
TEST_F(RouteExtensionIntegrationTest, NoExtensionLeavesTheRouteAlone) {
  initializeWithExtensionsAt(false, false, false);
  EXPECT_EQ(std::nullopt, upstreamTestHeader());
}

// All three levels compose. Each level wraps the route produced by the one before it, so the
// route configuration level ends up innermost and runs its header mutation first.
TEST_F(RouteExtensionIntegrationTest, AllLevelsComposeInOrder) {
  initializeWithExtensionsAt(true, true, true);
  EXPECT_EQ("config,vhost,route", upstreamTestHeader());
}

// The extensions run for a request that matched no route. This one declines to produce a route, so
// the request is still unroutable.
TEST_F(RouteExtensionIntegrationTest, ExtensionsRunWhenNothingMatched) {
  config_helper_.addConfigModifier([](HttpConnectionManager& hcm) {
    auto* route_config = hcm.mutable_route_config();
    *route_config->add_route_extensions() = extensionConfig("config");
    auto* vhost = route_config->mutable_virtual_hosts(0);
    *vhost->add_route_extensions() = extensionConfig("vhost");
    vhost->mutable_routes(0)->mutable_match()->set_prefix("/no-such-prefix");
  });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("404", response->headers().getStatusValue());
}

} // namespace
} // namespace Router
} // namespace Envoy
