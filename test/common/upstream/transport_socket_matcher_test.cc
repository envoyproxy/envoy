#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/scope.h"

#include "common/config/metadata.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/upstream/transport_socket_match_impl.h"

#include "server/transport_socket_config_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Upstream {
namespace {

class FakeTransportSocketFactory : public Network::TransportSocketFactory {
public:
  MOCK_METHOD(bool, implementsSecureTransport, (), (const));
  MOCK_METHOD(Network::TransportSocketPtr, createTransportSocket,
              (Network::TransportSocketOptionsSharedPtr), (const));
  FakeTransportSocketFactory(std::string id) : id_(std::move(id)) {}
  std::string id() const { return id_; }

private:
  const std::string id_;
};

class FooTransportSocketFactory
    : public Network::TransportSocketFactory,
      public Server::Configuration::UpstreamTransportSocketConfigFactory,
      Logger::Loggable<Logger::Id::upstream> {
public:
  MOCK_METHOD(bool, implementsSecureTransport, (), (const));
  MOCK_METHOD(Network::TransportSocketPtr, createTransportSocket,
              (Network::TransportSocketOptionsSharedPtr), (const));

  Network::TransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& proto,
                               Server::Configuration::TransportSocketFactoryContext&) override {
    const auto& node = dynamic_cast<const envoy::config::core::v3::Node&>(proto);
    std::string id = "default-foo";
    if (!node.id().empty()) {
      id = node.id();
    }
    return std::make_unique<FakeTransportSocketFactory>(id);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::core::v3::Node>();
  }

  std::string name() const override { return "foo"; }
};

class TransportSocketMatcherTest : public testing::Test {
public:
  TransportSocketMatcherTest()
      : registration_(factory_), mock_default_factory_(new FakeTransportSocketFactory("default")),
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
  Network::TransportSocketFactoryPtr mock_default_factory_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopePtr stats_scope_;
};

TEST_F(TransportSocketMatcherTest, ReturnDefaultSocketFactoryWhenNoMatch) {
  init({R"EOF(
name: "enableFooSocket"
match:
  hasSidecar: "true"
transport_socket:
  name: "foo"
  config:
    id: "abc"
 )EOF"});

  envoy::config::core::v3::Metadata metadata;
  validate(metadata, "default");
}

TEST_F(TransportSocketMatcherTest, BasicMatch) {
  init({R"EOF(
name: "sidecar_socket"
match:
  sidecar: "true"
transport_socket:
  name: "foo"
  config:
    id: "sidecar")EOF",
        R"EOF(
name: "http_socket"
match:
  protocol: "http"
transport_socket:
  name: "foo"
  config:
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
  config:
    id: "sidecar_http"
 )EOF",
        R"EOF(
name: "sidecar_socket"
match:
  sidecar: "true"
transport_socket:
  name: "foo"
  config:
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
  config:
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
