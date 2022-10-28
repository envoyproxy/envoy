#include <memory>
#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/scope.h"

#include "source/common/config/metadata.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/upstream/transport_socket_match_impl.h"
#include "source/server/transport_socket_config_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Upstream {
namespace {

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

  Network::UpstreamTransportSocketFactoryPtr
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
    matcher_ = std::make_unique<TransportSocketMatcherImpl>(matches, mock_factory_context_,
                                                            mock_default_factory_, *stats_scope_);
  }

  void validate(const envoy::config::core::v3::Metadata& metadata, const std::string& expected) {
    auto& factory = matcher_->resolve(&metadata).factory_;
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
  validate(metadata, "default");

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

  validate(metadata, "sidecar");
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket_match: { protocol: "http" }
)EOF",
                            metadata);
  validate(metadata, "http");
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
  validate(metadata, "sidecar_http");
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
  validate(metadata, "match_all");
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket: { random_label: "random_value" }
)EOF",
                            metadata);
  validate(metadata, "match_all");
}

} // namespace
} // namespace Upstream
} // namespace Envoy
