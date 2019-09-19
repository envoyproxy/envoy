#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/http/codec.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/config/metadata.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"
#include "common/upstream/transport_socket_matcher.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/network/utility.h"

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


class TransportSocketMatcherTest : public testing::Test {
public:
  TransportSocketMatcherTest() {
    //default_factory_ = new Network::MockTransportSocketFactory();
    //tls_factory_ = new Network::MockTransportSocketFactory();
    //rawbuffer_factory_ = new Network::MockTransportSocketFactory();
    //factory_map_ = new TransportSocketFactoryMap();
    //(*factory_map_)["tls"] = std::unique_ptr<Network::TransportSocketFactory>(tls_factory_);
    //(*factory_map_)["raw_buffer"] = std::unique_ptr<Network::TransportSocketFactory>(rawbuffer_factory_);
  }

  void init() {
    Protobuf::RepeatedPtrField<
      envoy::api::v2::Cluster_TransportSocketMatch> matches;
   std::vector<std::string> match_yaml = {R"EOF(
name: "enableMTLS"
match:
  hasSidecar: "true"
transport_socket:
  name: "tls"
  config:
    common_tls_context:
      tls_certificates:
      - certificate_chain: { filename: "cert-chain.pem" }
        private_key: { filename: "key.pem" }
)EOF"};

     for (const auto& yaml : match_yaml) {
       auto transport_socket_match = matches.Add();
       TestUtility::loadFromYaml(yaml, *transport_socket_match);
     }
    matcher_ = std::make_unique<TransportSocketMatcher>(
        matches, mock_factory_context_);
  }
protected:
  TransportSocketMatcherPtr matcher_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_context_;
  // Raw pointer since they will be owned by matcher_.
  //Network::MockTransportSocketFactory* default_factory_;
  //Network::MockTransportSocketFactory* tls_factory_;
  //Network::MockTransportSocketFactory* rawbuffer_factory_;
};

// This test ensures the matcher returns the default transport socket factory.
TEST_F(TransportSocketMatcherTest, ReturnDefaultSocketFactory) {
  init();
  envoy::api::v2::core::Metadata metadata;
  matcher_->resolve("hardcodenotexists", metadata);
}

//TODO: need to create more mock transport socket factory class and give them different name?
//otherwise, error as, Didn't find a registered implementation for name: 'tls'

} // namespace
} // namespace Usptream
} // namespace Envoy
