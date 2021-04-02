

#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/resource_manager.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/conn_pool.h"
#include "extensions/filters/network/mysql_proxy/conn_pool_impl.h"
#include "extensions/filters/network/mysql_proxy/message_helper.h"
#include "extensions/filters/network/mysql_proxy/mysql_client.h"
#include "extensions/filters/network/mysql_proxy/mysql_client_impl.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_update_callbacks.h"

#include "gtest/gtest.h"
#include "mock.h"
#include "mysql_test_utils.h"

using testing::NiceMock;
using testing::Sequence;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
namespace ConnectionPool {

class ConnPoolTest : public testing::Test, public DecoderFactory {
public:
  MOCK_METHOD((DecoderPtr), create, (DecoderCallbacks&));

  void setup() {}
  void teardown() { tls_.shutdownThread(); }

  void checkSize() {}

  void prepareDeocderCallbacks() {}

  MySQLSession session_;
  DecoderCallbacks* decoder_callbacks_{nullptr};
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;

  MockClient* client_{};
  Network::Address::InstanceConstSharedPtr test_address_;
  std::string auth_username_;
  std::string auth_password_;
  NiceMock<Api::MockApi> api_;
};

TEST_F(ConnPoolTest, BasicFailPoolClient) {
  setup();
  Sequence seq;

  teardown();
}

TEST_F(ConnPoolTest, FailByProtocolErr) {
  setup();
  Sequence seq;

  teardown();
}

TEST_F(ConnPoolTest, FailByNotHandledErrAtClientLoginResponse) {
  setup();

  teardown();
}

TEST_F(ConnPoolTest, FailByAuthErrAtClientLoginResponse) {
  setup();

  teardown();
}

TEST_F(ConnPoolTest, FailByNotSupportPluginErrAtClientLoginResponse) {
  setup();

  teardown();
}

TEST_F(ConnPoolTest, OkAtClientLoginResponse) {
  setup();

  teardown();
}

TEST_F(ConnPoolTest, FailByNotHandledErrAtMoreClientLoginResponse) {
  setup();
  Sequence seq;

  teardown();
}

TEST_F(ConnPoolTest, FailByAuthErrAtMoreClientLoginResponse) {
  setup();
  Sequence seq;

  teardown();
}

TEST_F(ConnPoolTest, OkAtMoreClientLoginResponse) {
  setup();
  Sequence seq;

  teardown();
}

TEST_F(ConnPoolTest, ImpossibleDeocderCallbackOnClientLogin) {
  setup();
  Sequence seq;

  teardown();
}

TEST_F(ConnPoolTest, ImpossibleDeocderCallbackOnSwithResponse) {
  setup();
  Sequence seq;

  teardown();
}

TEST_F(ConnPoolTest, ImpossibleDeocderCallbackOnCommand) {
  setup();
  Sequence seq;

  teardown();
}

TEST_F(ConnPoolTest, ImpossibleDeocderCallbackOnCommandResp) {
  setup();
  Sequence seq;

  teardown();
}

TEST_F(ConnPoolTest, GetClientFromStartClients) {
  setup();
  Sequence seq;

  teardown();
}

TEST_F(ConnPoolTest, CloseClient) {
  setup();
  Sequence seq;

  teardown();
}

TEST_F(ConnPoolTest, CloseMoreThanMaxIdleClients) {
  setup();
  Sequence seq;

  teardown();
}

TEST_F(ConnPoolTest, RequestMoreThanMaxConnection) {
  setup();
  Sequence seq;

  teardown();
}

TEST_F(ConnPoolTest, ThreadLocalPoolDeconstruct) {
  setup();
  Sequence seq;

  teardown();
}

} // namespace ConnectionPool
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy