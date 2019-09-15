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
    default_factory_ = new Network::MockTransportSocketFactory();
    factory_map_ = new TransportSocketFactoryMap();
    (*factory_map_)["tls"] = std::make_unique<Network::MockTransportSocketFactory>();
    (*factory_map_)["raw_buffer"] = std::make_unique<Network::MockTransportSocketFactory>();
  }

  void init() {
    matcher_ = std::make_unique<TransportSocketMatcher>(
        std::unique_ptr<Network::TransportSocketFactory>(default_factory_), 
        std::unique_ptr<TransportSocketFactoryMap>(factory_map_));
  }
protected:
  TransportSocketMatcherPtr matcher_;
  // Raw pointer since they will be owned by matcher_.
  Network::TransportSocketFactory* default_factory_;
  TransportSocketFactoryMap* factory_map_;
};

// This test ensures the matcher returns the default transport socket factory.
TEST_F(TransportSocketMatcherTest, ReturnDefaultSocketFactory) {
  // Get the raw pointer before constructing the matcher.
  //EXPECT_CALL(default_factory_, 
  init();
}

// TODO: defer when the matcher semantics is finalized.
TEST_F(TransportSocketMatcherTest, CustomizedSocketFactory) {
  init();
}

} // namespace
} // namespace Usptream
} // namespace Envoy
