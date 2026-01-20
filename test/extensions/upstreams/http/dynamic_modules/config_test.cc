#include "source/extensions/upstreams/http/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

class DynamicModuleGenericConnPoolFactoryTest : public ::testing::Test {
public:
  void SetUp() override {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);

    host_ = std::make_shared<NiceMock<Upstream::MockHost>>();
    cm_.initializeThreadLocalClusters({"fake_cluster"});
    EXPECT_CALL(cm_.thread_local_cluster_, tcpConnPool(_, _))
        .WillRepeatedly(Return(Upstream::TcpPoolData([]() {}, &mock_pool_)));
  }

  DynamicModuleGenericConnPoolFactory factory_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> host_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Envoy::Tcp::ConnectionPool::MockInstance mock_pool_;
};

TEST_F(DynamicModuleGenericConnPoolFactoryTest, FactoryName) {
  EXPECT_EQ(factory_.name(), "envoy.upstreams.http.dynamic_modules");
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, FactoryCategory) {
  EXPECT_EQ(factory_.category(), "envoy.upstreams");
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  EXPECT_NE(proto, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, CreateSuccess) {
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: http_tcp_bridge_no_op
    do_not_close: true
bridge_name: test_bridge
bridge_config:
    "@type": "type.googleapis.com/google.protobuf.StringValue"
    value: "test_config"
)EOF";

  envoy::extensions::upstreams::http::dynamic_modules::v3::DynamicModuleHttpTcpBridgeConfig config;
  TestUtility::loadFromYamlAndValidate(yaml, config);

  auto conn_pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);

  EXPECT_NE(conn_pool, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, CreateWithEmptyConfig) {
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: http_tcp_bridge_no_op
bridge_name: test_bridge
)EOF";

  envoy::extensions::upstreams::http::dynamic_modules::v3::DynamicModuleHttpTcpBridgeConfig config;
  TestUtility::loadFromYamlAndValidate(yaml, config);

  auto conn_pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);

  EXPECT_NE(conn_pool, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, CreateWithBytesConfig) {
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: http_tcp_bridge_no_op
bridge_name: test_bridge
bridge_config:
    "@type": "type.googleapis.com/google.protobuf.BytesValue"
    value: "dGVzdA=="  # echo -n "test" | base64
)EOF";

  envoy::extensions::upstreams::http::dynamic_modules::v3::DynamicModuleHttpTcpBridgeConfig config;
  TestUtility::loadFromYamlAndValidate(yaml, config);

  auto conn_pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);

  EXPECT_NE(conn_pool, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, CreateWithStructConfig) {
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: http_tcp_bridge_no_op
bridge_name: test_bridge
bridge_config:
    "@type": "type.googleapis.com/google.protobuf.Struct"
    value:
        key: value
)EOF";

  envoy::extensions::upstreams::http::dynamic_modules::v3::DynamicModuleHttpTcpBridgeConfig config;
  TestUtility::loadFromYamlAndValidate(yaml, config);

  auto conn_pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);

  EXPECT_NE(conn_pool, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, CreateFailureNonHttpProtocol) {
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: http_tcp_bridge_no_op
bridge_name: test_bridge
)EOF";

  envoy::extensions::upstreams::http::dynamic_modules::v3::DynamicModuleHttpTcpBridgeConfig config;
  TestUtility::loadFromYamlAndValidate(yaml, config);

  // TCP protocol should return nullptr.
  auto conn_pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::TCP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);

  EXPECT_EQ(conn_pool, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, CreateFailureModuleNotFound) {
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: non_existent_module
bridge_name: test_bridge
)EOF";

  envoy::extensions::upstreams::http::dynamic_modules::v3::DynamicModuleHttpTcpBridgeConfig config;
  TestUtility::loadFromYamlAndValidate(yaml, config);

  auto conn_pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);

  EXPECT_EQ(conn_pool, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, CreateFailureConfigNewFails) {
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: http_tcp_bridge_config_new_fail
bridge_name: test_bridge
)EOF";

  envoy::extensions::upstreams::http::dynamic_modules::v3::DynamicModuleHttpTcpBridgeConfig config;
  TestUtility::loadFromYamlAndValidate(yaml, config);

  auto conn_pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);

  EXPECT_EQ(conn_pool, nullptr);
}

class DynamicModuleGenericConnPoolFactoryNoPoolTest : public ::testing::Test {
public:
  void SetUp() override {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);

    host_ = std::make_shared<NiceMock<Upstream::MockHost>>();
    cm_.initializeThreadLocalClusters({"fake_cluster"});
    // Return nullopt to simulate no TCP pool available.
    EXPECT_CALL(cm_.thread_local_cluster_, tcpConnPool(_, _)).WillOnce(Return(absl::nullopt));
  }

  DynamicModuleGenericConnPoolFactory factory_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> host_;
  NiceMock<Upstream::MockClusterManager> cm_;
};

TEST_F(DynamicModuleGenericConnPoolFactoryNoPoolTest, CreateFailureNoPool) {
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: http_tcp_bridge_no_op
bridge_name: test_bridge
)EOF";

  envoy::extensions::upstreams::http::dynamic_modules::v3::DynamicModuleHttpTcpBridgeConfig config;
  TestUtility::loadFromYamlAndValidate(yaml, config);

  auto conn_pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);

  // Should return nullptr because the TCP pool is not available.
  EXPECT_EQ(conn_pool, nullptr);
}

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
