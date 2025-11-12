#include <memory>
#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/common/optref.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/scope.h"

#include "source/common/config/metadata.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/upstream/transport_socket_match_impl.h"
#include "source/server/transport_socket_config_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xds/type/matcher/v3/matcher.pb.h"

using testing::NiceMock;

namespace Envoy {
namespace Upstream {
namespace {

using Extensions::Matching::CommonInputs::TransportSocket::TransportSocketNameAction;
using Extensions::Matching::CommonInputs::TransportSocket::TransportSocketNameActionFactory;

class FakeTransportSocketFactory : public Network::UpstreamTransportSocketFactory {
public:
  MOCK_METHOD(bool, implementsSecureTransport, (), (const));
  MOCK_METHOD(bool, supportsAlpn, (), (const));
  MOCK_METHOD(absl::string_view, defaultServerNameIndication, (), (const));
  MOCK_METHOD(Network::TransportSocketPtr, createTransportSocket,
              (Network::TransportSocketOptionsConstSharedPtr,
               Upstream::HostDescriptionConstSharedPtr),
              (const));
  MOCK_METHOD(void, hashKey, (std::vector<uint8_t>&, Network::TransportSocketOptionsConstSharedPtr),
              (const));
  FakeTransportSocketFactory(std::string id, bool alpn) : supports_alpn_(alpn), id_(std::move(id)) {
    ON_CALL(*this, supportsAlpn).WillByDefault(Invoke([this]() { return supports_alpn_; }));
  }
  std::string id() const { return id_; }

  bool supports_alpn_;

private:
  const std::string id_;
};

class FooTransportSocketFactory
    : public Network::UpstreamTransportSocketFactory,
      public Server::Configuration::UpstreamTransportSocketConfigFactory,
      Logger::Loggable<Logger::Id::upstream> {
public:
  MOCK_METHOD(bool, implementsSecureTransport, (), (const));
  MOCK_METHOD(Network::TransportSocketPtr, createTransportSocket,
              (Network::TransportSocketOptionsConstSharedPtr,
               Upstream::HostDescriptionConstSharedPtr),
              (const));
  MOCK_METHOD(void, hashKey, (std::vector<uint8_t>&, Network::TransportSocketOptionsConstSharedPtr),
              (const));
  MOCK_METHOD(absl::string_view, defaultServerNameIndication, (), (const));

  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
  createTransportSocketFactory(const Protobuf::Message& proto,
                               Server::Configuration::TransportSocketFactoryContext&) override {
    const auto& node = dynamic_cast<const envoy::config::core::v3::Node&>(proto);
    std::string id = "default-foo";
    if (!node.id().empty()) {
      id = node.id();
    }
    return std::make_unique<NiceMock<FakeTransportSocketFactory>>(id, supports_alpn_);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::core::v3::Node>();
  }

  std::string name() const override { return "foo"; }
  bool supports_alpn_{};
};

class TransportSocketMatcherTest : public testing::Test {
public:
  TransportSocketMatcherTest()
      : registration_(factory_),
        mock_default_factory_(new NiceMock<FakeTransportSocketFactory>("default", false)),
        stats_scope_(stats_store_.createScope("transport_socket_match.test")) {}

  void init(const std::vector<std::string>& match_yaml) {
    Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch> matches;
    for (const auto& yaml : match_yaml) {
      auto transport_socket_match = matches.Add();
      TestUtility::loadFromYaml(yaml, *transport_socket_match);
    }
    matcher_ = TransportSocketMatcherImpl::create(matches, mock_factory_context_,
                                                  mock_default_factory_, *stats_scope_)
                   .value();
  }

  void validate(const envoy::config::core::v3::Metadata* endpoint_metadata,
                const envoy::config::core::v3::Metadata* locality_metadata,
                const std::string& expected) {
    auto& factory = matcher_->resolve(endpoint_metadata, locality_metadata).factory_;
    const auto& config_factory = dynamic_cast<const FakeTransportSocketFactory&>(factory);
    EXPECT_EQ(expected, config_factory.id());
  }

protected:
  FooTransportSocketFactory factory_;
  Registry::InjectFactory<Server::Configuration::UpstreamTransportSocketConfigFactory>
      registration_;

  TransportSocketMatcherPtr matcher_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_context_;
  Network::UpstreamTransportSocketFactoryPtr mock_default_factory_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
};

TEST_F(TransportSocketMatcherTest, ReturnDefaultSocketFactoryWhenNoMatch) {
  init({R"EOF(
name: "enableFooSocket"
match:
  hasSidecar: "true"
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "abc"
 )EOF"});

  envoy::config::core::v3::Metadata metadata;
  validate(&metadata, nullptr, "default");

  // Neither the defaults nor matcher support ALPN.
  EXPECT_FALSE(matcher_->allMatchesSupportAlpn());
}

TEST_F(TransportSocketMatcherTest, AlpnSupport) {
  mock_default_factory_ = std::make_unique<NiceMock<FakeTransportSocketFactory>>("default", true);
  factory_.supports_alpn_ = true;
  init({R"EOF(
name: "enableFooSocket"
match:
  hasSidecar: "true"
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "abc"
 )EOF"});

  // Both default and matcher support ALPN
  EXPECT_TRUE(matcher_->allMatchesSupportAlpn());
}

TEST_F(TransportSocketMatcherTest, NoDefaultAlpnSupport) {
  factory_.supports_alpn_ = true;
  init({R"EOF(
name: "enableFooSocket"
match:
  hasSidecar: "true"
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "abc"
 )EOF"});

  // The default doesn't support ALPN, matchers do.
  EXPECT_FALSE(matcher_->allMatchesSupportAlpn());
}

TEST_F(TransportSocketMatcherTest, NoMatcherAlpnSupport) {
  mock_default_factory_ = std::make_unique<NiceMock<FakeTransportSocketFactory>>("default", true);
  init({R"EOF(
name: "enableFooSocket"
match:
  hasSidecar: "true"
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "abc"
 )EOF"});

  // The default doesn't support ALPN, matchers do.
  EXPECT_FALSE(matcher_->allMatchesSupportAlpn());
}

TEST_F(TransportSocketMatcherTest, BasicMatch) {
  init({R"EOF(
name: "sidecar_socket"
match:
  sidecar: "true"
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "sidecar")EOF",
        R"EOF(
name: "http_socket"
match:
  protocol: "http"
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "http"
 )EOF"});

  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket_match: { sidecar: "true" }
)EOF",
                            metadata);

  validate(&metadata, nullptr, "sidecar");
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket_match: { protocol: "http" }
)EOF",
                            metadata);
  validate(&metadata, nullptr, "http");
}

// New: xDS matcher using endpoint metadata (envoy.lb -> type) to select sockets "tls"/"raw".
TEST_F(TransportSocketMatcherTest, XdsMatcherEndpointMetadata) {
  // Build socket matches for tls/raw using the "foo" config factory with distinct IDs.
  Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch> matches;
  {
    auto* m = matches.Add();
    TestUtility::loadFromYaml(R"EOF(
name: "tls"
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "tls_id"
)EOF",
                              *m);
  }
  {
    auto* m = matches.Add();
    TestUtility::loadFromYaml(R"EOF(
name: "raw"
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "raw_id"
)EOF",
                              *m);
  }

  // Build xDS matcher reading endpoint metadata key envoy.lb path [type].
  xds::type::matcher::v3::Matcher matcher;
  TestUtility::loadFromYaml(R"EOF(
matcher_tree:
  input:
    name: envoy.matching.inputs.endpoint_metadata
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.transport_socket.v3.EndpointMetadataInput
      filter: envoy.lb
      path:
      - key: type
  exact_match_map:
    map:
      "tls":
        action:
          name: envoy.matching.action.transport_socket.name
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.transport_socket.v3.TransportSocketNameAction
            name: tls
      "raw":
        action:
          name: envoy.matching.action.transport_socket.name
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.transport_socket.v3.TransportSocketNameAction
            name: raw
)EOF",
                            matcher);

  // Create matcher instance with xDS configuration.
  matcher_ = TransportSocketMatcherImpl::create(matches, makeOptRefFromPtr(&matcher),
                                                mock_factory_context_, mock_default_factory_,
                                                *stats_scope_)
                 .value();

  // Endpoint metadata selects tls.
  envoy::config::core::v3::Metadata endpoint_metadata;
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.lb: { type: "tls" }
)EOF",
                            endpoint_metadata);
  auto& factory_tls = matcher_->resolve(&endpoint_metadata, nullptr).factory_;
  const auto& foo_tls = dynamic_cast<const FakeTransportSocketFactory&>(factory_tls);
  EXPECT_EQ("tls_id", foo_tls.id());

  // Endpoint metadata selects raw.
  envoy::config::core::v3::Metadata endpoint_metadata2;
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.lb: { type: "raw" }
)EOF",
                            endpoint_metadata2);
  auto& factory_raw = matcher_->resolve(&endpoint_metadata2, nullptr).factory_;
  const auto& foo_raw = dynamic_cast<const FakeTransportSocketFactory&>(factory_raw);
  EXPECT_EQ("raw_id", foo_raw.id());
}

TEST_F(TransportSocketMatcherTest, MultipleMatchFirstWin) {
  init({R"EOF(
name: "sidecar_http_socket"
match:
  sidecar: "true"
  protocol: "http"
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "sidecar_http"
 )EOF",
        R"EOF(
name: "sidecar_socket"
match:
  sidecar: "true"
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "sidecar"
 )EOF"});
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket_match: { sidecar: "true", protocol: "http" }
)EOF",
                            metadata);
  validate(&metadata, nullptr, "sidecar_http");
}

TEST_F(TransportSocketMatcherTest, MatchAllEndpointsFactory) {
  init({R"EOF(
name: "match_all"
match: {}
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "match_all"
 )EOF"});
  envoy::config::core::v3::Metadata metadata;
  validate(&metadata, nullptr, "match_all");
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket_match: { random_label: "random_value" }
)EOF",
                            metadata);
  validate(&metadata, nullptr, "match_all");
}

TEST_F(TransportSocketMatcherTest, VerifyNulls) {
  init({R"EOF(
name: "match_all"
match: {}
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "match_all"
 )EOF"});

  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket_match: { whatever: "whatever" }
)EOF",
                            metadata);

  validate(nullptr, nullptr, "match_all");
  validate(&metadata, nullptr, "match_all");
  validate(nullptr, &metadata, "match_all");
  validate(&metadata, &metadata, "match_all");
}

// Test that matching on locality metadata works.
TEST_F(TransportSocketMatcherTest, LocalityMatching) {
  init({R"EOF(
name: "one"
match:
  whodis: "one"
transport_socket:
  name: "one"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "socket1"
 )EOF",
        R"EOF(
name: "two"
match:
  whodis: "two"
transport_socket:
  name: "two"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "socket2"
 )EOF"});

  envoy::config::core::v3::Metadata locality_metadata;
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket_match: { whodis: "two" }
)EOF",
                            locality_metadata);

  // The endpoint and locality metadata will match different transport sockets, so let's make sure
  // that the locality matching works at all.
  validate(nullptr, &locality_metadata, "socket2");
}

// Test that the endpoint metadata takes precedence over locality metadata.
TEST_F(TransportSocketMatcherTest, EndpointMetadataPrecedence) {
  init({R"EOF(
name: "endpoint"
match:
  whodis: "endpoint"
transport_socket:
  name: "endpoint"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "endpoint_match"
 )EOF",
        R"EOF(
name: "locality"
match:
  whodis: "locality"
transport_socket:
  name: "locality"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "locality_match"
 )EOF"});

  envoy::config::core::v3::Metadata endpoint_metadata;
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket_match: { whodis: "endpoint" }
)EOF",
                            endpoint_metadata);

  envoy::config::core::v3::Metadata locality_metadata;
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket_match: { whodis: "locality" }
)EOF",
                            locality_metadata);

  // The endpoint and locality metadata will match to different transport sockets, so let's make
  // sure that the endpoint metadata is the one that is matched.
  validate(&endpoint_metadata, &locality_metadata, "endpoint_match");

  // Let's also make sure that this isn't some behavior caused by the ordering of the match
  // statements. If we present the locality metadata as the endpoint's metadata, it should match on
  // "locality_match" instead.
  validate(&locality_metadata, &endpoint_metadata, "locality_match");
}

TEST_F(TransportSocketMatcherTest, TransportSocketNameActionFactoryAndTypeUrl) {
  // Build a TransportSocketNameAction config and create the action via the factory.
  TransportSocketNameActionFactory factory;
  envoy::extensions::matching::common_inputs::transport_socket::v3::TransportSocketNameAction cfg;
  cfg.set_name("tls");
  auto& visitor = ProtobufMessage::getNullValidationVisitor();
  auto action = factory.createAction(cfg, mock_factory_context_.serverFactoryContext(), visitor);
  ASSERT_NE(action, nullptr);

  // Verify the action holds the expected name and exposes the declared type URL.
  const auto& typed = action->getTyped<TransportSocketNameAction>();
  EXPECT_EQ(typed.name(), "tls");
}

TEST_F(TransportSocketMatcherTest, XdsMatcherAnyMatcherWhenTypeNotSet) {
  // Provide sockets.
  Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch> matches;
  auto* m_tls = matches.Add();
  TestUtility::loadFromYaml(R"EOF(
name: "tls"
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "tls_id"
)EOF",
                            *m_tls);

  // Matcher with MATCHER_TYPE_NOT_SET triggers AnyMatcher path -> default.
  xds::type::matcher::v3::Matcher matcher; // left empty intentionally
  auto result_or = TransportSocketMatcherImpl::create(matches, makeOptRefFromPtr(&matcher),
                                                      mock_factory_context_, mock_default_factory_,
                                                      *stats_scope_);
  ASSERT_TRUE(result_or.ok()) << result_or.status();
  matcher_ = std::move(*result_or);

  auto& factory = matcher_->resolve(nullptr, nullptr).factory_;
  const auto& f = dynamic_cast<const FakeTransportSocketFactory&>(factory);
  EXPECT_EQ("default", f.id());
}

TEST_F(TransportSocketMatcherTest, SetupMatcherErrorsMissingNameAndDuplicate) {
  // Missing name should return InvalidArgument.
  {
    Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch> matches;
    auto* m = matches.Add();
    TestUtility::loadFromYaml(R"EOF(
match: {}
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "x"
)EOF",
                              *m);
    xds::type::matcher::v3::Matcher matcher; // any matcher present triggers setup path
    auto created = TransportSocketMatcherImpl::create(matches, makeOptRefFromPtr(&matcher),
                                                      mock_factory_context_, mock_default_factory_,
                                                      *stats_scope_);
    EXPECT_FALSE(created.ok());
    EXPECT_TRUE(absl::IsInvalidArgument(created.status())) << created.status();
  }

  // Duplicate names should return InvalidArgument.
  {
    Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch> matches;
    auto* m1 = matches.Add();
    TestUtility::loadFromYaml(R"EOF(
name: "dup"
match: {}
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "a"
)EOF",
                              *m1);
    auto* m2 = matches.Add();
    TestUtility::loadFromYaml(R"EOF(
name: "dup"
match: {}
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "b"
)EOF",
                              *m2);

    xds::type::matcher::v3::Matcher matcher; // triggers setup path
    auto created = TransportSocketMatcherImpl::create(matches, makeOptRefFromPtr(&matcher),
                                                      mock_factory_context_, mock_default_factory_,
                                                      *stats_scope_);
    EXPECT_FALSE(created.ok());
    EXPECT_TRUE(absl::IsInvalidArgument(created.status())) << created.status();
  }
}

// End-to-end test for filter state-based transport socket selection.
TEST_F(TransportSocketMatcherTest, XdsMatcherFilterStateEndToEnd) {
  // Create two named transport sockets.
  Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch> matches;
  auto* m_namespace_a = matches.Add();
  TestUtility::loadFromYaml(R"EOF(
name: "namespace_a_socket"
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "namespace_a_id"
)EOF",
                            *m_namespace_a);
  auto* m_namespace_b = matches.Add();
  TestUtility::loadFromYaml(R"EOF(
name: "namespace_b_socket"
transport_socket:
  name: "foo"
  typed_config:
    "@type": type.googleapis.com/envoy.config.core.v3.Node
    id: "namespace_b_id"
)EOF",
                            *m_namespace_b);

  // Create xDS matcher using transport_socket_filter_state input for network namespace.
  xds::type::matcher::v3::Matcher matcher;
  TestUtility::loadFromYaml(R"EOF(
matcher_tree:
  input:
    name: envoy.matching.inputs.transport_socket_filter_state
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.transport_socket.v3.FilterStateInput
      key: "envoy.network.namespace"
  exact_match_map:
    map:
      "/run/netns/namespace-a":
        action:
          name: envoy.matching.action.transport_socket.name
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.transport_socket.v3.TransportSocketNameAction
            name: namespace_a_socket
      "/run/netns/namespace-b":
        action:
          name: envoy.matching.action.transport_socket.name
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.transport_socket.v3.TransportSocketNameAction
            name: namespace_b_socket
on_no_match:
  action:
    name: envoy.matching.action.transport_socket.name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.transport_socket.v3.TransportSocketNameAction
      name: namespace_a_socket
)EOF",
                            matcher);

  matcher_ = TransportSocketMatcherImpl::create(matches, makeOptRefFromPtr(&matcher),
                                                mock_factory_context_, mock_default_factory_,
                                                *stats_scope_)
                 .value();

  // Test 1: Create TransportSocketOptions with filter state for namespace-a.
  {
    auto downstream_filter_state = std::make_shared<StreamInfo::FilterStateImpl>(
        StreamInfo::FilterState::LifeSpan::Connection);

    // Simulate a filter setting network namespace in downstream filter state.
    auto ns_object = std::make_shared<Router::StringAccessorImpl>("/run/netns/namespace-a");
    downstream_filter_state->setData(
        "envoy.network.namespace", ns_object, StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::Connection,
        StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);

    // Create TransportSocketOptions with the shared filter state objects.
    auto shared_objects = downstream_filter_state->objectsSharedWithUpstreamConnection();
    auto transport_socket_options = std::make_shared<Network::TransportSocketOptionsImpl>(
        "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
        absl::nullopt, std::move(shared_objects));

    // Resolve transport socket - should select namespace_a_socket.
    auto result = matcher_->resolve(nullptr, nullptr, transport_socket_options);
    const auto& factory = dynamic_cast<const FakeTransportSocketFactory&>(result.factory_);
    EXPECT_EQ("namespace_a_id", factory.id());
    EXPECT_EQ("namespace_a_socket", result.name_);
  }

  // Test 2: TransportSocketOptions with filter state for namespace-b.
  {
    auto downstream_filter_state = std::make_shared<StreamInfo::FilterStateImpl>(
        StreamInfo::FilterState::LifeSpan::Connection);

    auto ns_object = std::make_shared<Router::StringAccessorImpl>("/run/netns/namespace-b");
    downstream_filter_state->setData(
        "envoy.network.namespace", ns_object, StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::Connection,
        StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);

    auto shared_objects = downstream_filter_state->objectsSharedWithUpstreamConnection();
    auto transport_socket_options = std::make_shared<Network::TransportSocketOptionsImpl>(
        "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
        absl::nullopt, std::move(shared_objects));

    auto result = matcher_->resolve(nullptr, nullptr, transport_socket_options);
    const auto& factory = dynamic_cast<const FakeTransportSocketFactory&>(result.factory_);
    EXPECT_EQ("namespace_b_id", factory.id());
    EXPECT_EQ("namespace_b_socket", result.name_);
  }

  // Test 3: No TransportSocketOptions. It should fall back to on_no_match.
  {
    auto result = matcher_->resolve(nullptr, nullptr, nullptr);
    const auto& factory = dynamic_cast<const FakeTransportSocketFactory&>(result.factory_);
    EXPECT_EQ("namespace_a_id", factory.id()); // on_no_match defaults to namespace_a_socket
    EXPECT_EQ("namespace_a_socket", result.name_);
  }

  // Test 4: TransportSocketOptions with empty filter state - should fall back to on_no_match.
  {
    auto downstream_filter_state = std::make_shared<StreamInfo::FilterStateImpl>(
        StreamInfo::FilterState::LifeSpan::Connection);
    auto shared_objects = downstream_filter_state->objectsSharedWithUpstreamConnection();
    auto transport_socket_options = std::make_shared<Network::TransportSocketOptionsImpl>(
        "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
        absl::nullopt, std::move(shared_objects));

    auto result = matcher_->resolve(nullptr, nullptr, transport_socket_options);
    const auto& factory = dynamic_cast<const FakeTransportSocketFactory&>(result.factory_);
    EXPECT_EQ("namespace_a_id", factory.id()); // on_no_match defaults to namespace_a_socket
    EXPECT_EQ("namespace_a_socket", result.name_);
  }
}

} // namespace
} // namespace Upstream
} // namespace Envoy
