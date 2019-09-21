#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/http/codec.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/config/metadata.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/network/utility.h"
#include "common/upstream/transport_socket_matcher.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Upstream {
namespace {

class FooTransportSocketFactory : public Network::TransportSocketFactory,
        public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  FooTransportSocketFactory() {}
  ~FooTransportSocketFactory() override {}

  MOCK_CONST_METHOD0(implementsSecureTransport, bool());
  MOCK_CONST_METHOD1(createTransportSocket, Network::TransportSocketPtr(Network::TransportSocketOptionsSharedPtr));

  Network::TransportSocketFactoryPtr createTransportSocketFactory(
        const Protobuf::Message&, Server::Configuration::TransportSocketFactoryContext&) override {
    return std::make_unique<FooTransportSocketFactory>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // TODO: here is wrong... need to create  another tyed proto, maybe struct, not transportsocket here...
    return std::make_unique<envoy::api::v2::core::TransportSocket>();
  }

  std::string name() const override { return "foo"; }
};


class BarTransportSocketFactory : public Network::TransportSocketFactory,
        public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  BarTransportSocketFactory() {}
  ~BarTransportSocketFactory() override {}

  MOCK_CONST_METHOD0(implementsSecureTransport, bool());
  MOCK_CONST_METHOD1(createTransportSocket, Network::TransportSocketPtr(Network::TransportSocketOptionsSharedPtr));

  Network::TransportSocketFactoryPtr createTransportSocketFactory(
        const Protobuf::Message&, Server::Configuration::TransportSocketFactoryContext&) override {
    return std::make_unique<BarTransportSocketFactory>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::api::v2::core::TransportSocket>();
  }

  std::string name() const override { return "bar"; }
};


class TransportSocketMatcherTest : public testing::Test {
public:
  TransportSocketMatcherTest() {
    Protobuf::RepeatedPtrField<envoy::api::v2::Cluster_TransportSocketMatch> matches;
    std::vector<std::string> match_yaml = {R"EOF(
name: "enableFooSocket"
match:
  hasSidecar: "true"
transport_socket:
  name: "foo"
  config: {}
 )EOF", R"EOF(
name: "defaultToBarSocket"
match: {}
transport_socket:
  name: "bar"
  config: {}
)EOF"
    };
    for (const auto& yaml : match_yaml) {
      auto transport_socket_match = matches.Add();
      TestUtility::loadFromYaml(yaml, *transport_socket_match);
    }
    matcher_ = std::make_unique<TransportSocketMatcher>(matches, mock_factory_context_);
  }

protected:
  TransportSocketMatcherPtr matcher_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_context_;
};

// This test ensures the matcher returns the default transport socket factory.
TEST_F(TransportSocketMatcherTest, ReturnDefaultSocketFactory) {
  envoy::api::v2::core::Metadata metadata;
  matcher_->resolve("10.0.0.1", metadata);
  //auto config_factory = dynamic_cast<Server::Configuration::UpstreamTransportSocketConfigFactory>(socket);
  //EXPECT_EQ("bar", config_factory.name());
}

REGISTER_FACTORY(FooTransportSocketFactory,
    Server::Configuration::UpstreamTransportSocketConfigFactory);


REGISTER_FACTORY(BarTransportSocketFactory,
    Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace
} // namespace Upstream
} // namespace Envoy
