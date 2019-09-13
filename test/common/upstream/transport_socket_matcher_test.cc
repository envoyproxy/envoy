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
    //matcher_ = std::make_unique<TransportSocketMatcher>(default_fatory_);
  }
protected:
  TransportSocketMatcherPtr matcher_;
  // Weak pointer?
  //testing::NiceMock<MockTransportSocketFactory> default_fatory_;
  //testing::NiceMock<MockTransportSocketFactory> factory_a_;
  //testing::NiceMock<MockTransportSocketFactory> factory_b_;
};

// This test ensures the matcher returns the default transport socket factory.
TEST_F(TransportSocketMatcherTest, ReturnDefaultSocketFactory) {
}

// TODO: defer when the matcher semantics is finalized.
TEST_F(TransportSocketMatcherTest, CustomizedSocketFactory) {
}

} // namespace
} // namespace Usptream
} // namespace Envoy
