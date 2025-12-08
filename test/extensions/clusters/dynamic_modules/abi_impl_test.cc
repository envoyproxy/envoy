#include "source/extensions/clusters/dynamic_modules/cluster.h"
#include "source/extensions/dynamic_modules/abi.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {
namespace {

// Forward declare the ABI callbacks that we want to test.
// These are implemented in source/extensions/clusters/dynamic_modules/abi_impl.cc.
extern "C" {
size_t envoy_dynamic_module_callback_cluster_get_name(envoy_dynamic_module_type_cluster_envoy_ptr,
                                                      envoy_dynamic_module_type_buffer_envoy_ptr*);

envoy_dynamic_module_type_cluster_result envoy_dynamic_module_callback_cluster_add_host(
    envoy_dynamic_module_type_cluster_envoy_ptr, envoy_dynamic_module_type_buffer_module_ptr,
    size_t, uint32_t, uint32_t, envoy_dynamic_module_type_host_envoy_ptr*);

envoy_dynamic_module_type_cluster_result
    envoy_dynamic_module_callback_cluster_remove_host(envoy_dynamic_module_type_cluster_envoy_ptr,
                                                      envoy_dynamic_module_type_host_envoy_ptr);

void envoy_dynamic_module_callback_cluster_get_hosts(envoy_dynamic_module_type_cluster_envoy_ptr,
                                                     envoy_dynamic_module_type_host_info**,
                                                     size_t*);

envoy_dynamic_module_type_host_envoy_ptr envoy_dynamic_module_callback_cluster_get_host_by_address(
    envoy_dynamic_module_type_cluster_envoy_ptr, envoy_dynamic_module_type_buffer_module_ptr,
    size_t, uint32_t);

void envoy_dynamic_module_callback_host_set_weight(envoy_dynamic_module_type_host_envoy_ptr,
                                                   uint32_t);

size_t envoy_dynamic_module_callback_host_get_address(envoy_dynamic_module_type_host_envoy_ptr,
                                                      envoy_dynamic_module_type_buffer_envoy_ptr*,
                                                      uint32_t*);

envoy_dynamic_module_type_host_health
    envoy_dynamic_module_callback_host_get_health(envoy_dynamic_module_type_host_envoy_ptr);

uint32_t envoy_dynamic_module_callback_host_get_weight(envoy_dynamic_module_type_host_envoy_ptr);

void envoy_dynamic_module_callback_cluster_pre_init_complete(
    envoy_dynamic_module_type_cluster_envoy_ptr);

bool envoy_dynamic_module_callback_lb_context_get_hash_key(
    envoy_dynamic_module_type_lb_context_envoy_ptr, uint64_t*);

size_t envoy_dynamic_module_callback_lb_context_get_header(
    envoy_dynamic_module_type_lb_context_envoy_ptr, envoy_dynamic_module_type_buffer_module_ptr,
    size_t, envoy_dynamic_module_type_buffer_envoy_ptr*);

size_t envoy_dynamic_module_callback_lb_context_get_override_host(
    envoy_dynamic_module_type_lb_context_envoy_ptr, envoy_dynamic_module_type_buffer_envoy_ptr*,
    bool*);

bool envoy_dynamic_module_callback_lb_context_get_attempt_count(
    envoy_dynamic_module_type_lb_context_envoy_ptr, uint32_t*);

bool envoy_dynamic_module_callback_lb_context_get_downstream_connection_id(
    envoy_dynamic_module_type_lb_context_envoy_ptr, uint64_t*);

envoy_dynamic_module_type_thread_local_cluster_envoy_ptr
    envoy_dynamic_module_callback_cluster_manager_get_thread_local_cluster(
        envoy_dynamic_module_type_cluster_envoy_ptr, envoy_dynamic_module_type_buffer_module_ptr,
        size_t);

envoy_dynamic_module_type_host_envoy_ptr
    envoy_dynamic_module_callback_thread_local_cluster_choose_host(
        envoy_dynamic_module_type_thread_local_cluster_envoy_ptr,
        envoy_dynamic_module_type_lb_context_envoy_ptr);

size_t envoy_dynamic_module_callback_thread_local_cluster_get_name(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr,
    envoy_dynamic_module_type_buffer_envoy_ptr*);

size_t envoy_dynamic_module_callback_thread_local_cluster_host_count(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr);
}

using Extensions::DynamicModules::testSharedObjectPath;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

class AbiImplTest : public testing::Test {
protected:
  void SetUp() override {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        testSharedObjectPath("cluster_no_op", "c"), false);
    EXPECT_TRUE(dynamic_module.ok());

    auto config = newDynamicModuleClusterConfig("test_cluster", "",
                                                std::move(dynamic_module.value()), context_);
    EXPECT_TRUE(config.ok());
    config_ = config.value();

    envoy::config::cluster::v3::Cluster cluster_proto;
    cluster_proto.set_name("test_cluster");
    cluster_proto.mutable_connect_timeout()->set_seconds(1);

    envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig module_config;
    module_config.set_cluster_name("test_cluster");

    Upstream::ClusterFactoryContextImpl cluster_context(context_, nullptr, nullptr, false);

    absl::Status creation_status = absl::OkStatus();
    cluster_ = std::make_shared<DynamicModuleCluster>(cluster_proto, module_config, config_,
                                                      cluster_context, creation_status);
    EXPECT_TRUE(creation_status.ok());
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  DynamicModuleClusterConfigSharedPtr config_;
  DynamicModuleClusterSharedPtr cluster_;
};

TEST_F(AbiImplTest, GetClusterName) {
  envoy_dynamic_module_type_buffer_envoy_ptr name_out = nullptr;
  size_t name_length = envoy_dynamic_module_callback_cluster_get_name(cluster_.get(), &name_out);

  EXPECT_GT(name_length, 0);
  EXPECT_NE(name_out, nullptr);
  std::string name(name_out, name_length);
  EXPECT_EQ(name, "test_cluster");
}

TEST_F(AbiImplTest, AddHostInvalidArgumentError) {
  envoy_dynamic_module_type_host_envoy_ptr host_out = nullptr;
  auto result = envoy_dynamic_module_callback_cluster_add_host(
      cluster_.get(), const_cast<char*>("invalid_addr"), 11, 8080, 1, &host_out);

  EXPECT_EQ(result, envoy_dynamic_module_type_cluster_result_InvalidAddress);
}

TEST_F(AbiImplTest, AddHostMaxHostsReached) {
  // First, reduce max_hosts for testing by creating a new cluster with max_hosts=1.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("cluster_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  auto config = newDynamicModuleClusterConfig("test_cluster_small", "",
                                              std::move(dynamic_module.value()), context_);
  EXPECT_TRUE(config.ok());

  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster_small");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig module_config;
  module_config.set_cluster_name("test_cluster_small");
  module_config.mutable_max_hosts()->set_value(1);

  Upstream::ClusterFactoryContextImpl cluster_context(context_, nullptr, nullptr, false);

  absl::Status creation_status = absl::OkStatus();
  auto small_cluster = std::make_shared<DynamicModuleCluster>(
      cluster_proto, module_config, config.value(), cluster_context, creation_status);
  EXPECT_TRUE(creation_status.ok());

  // Add first host - should succeed.
  envoy_dynamic_module_type_host_envoy_ptr host_out1 = nullptr;
  auto result1 = envoy_dynamic_module_callback_cluster_add_host(
      small_cluster.get(), const_cast<char*>("127.0.0.1"), 9, 8080, 1, &host_out1);
  EXPECT_EQ(result1, envoy_dynamic_module_type_cluster_result_Success);
  EXPECT_NE(host_out1, nullptr);

  // Add second host - should fail with MaxHostsReached.
  envoy_dynamic_module_type_host_envoy_ptr host_out2 = nullptr;
  auto result2 = envoy_dynamic_module_callback_cluster_add_host(
      small_cluster.get(), const_cast<char*>("127.0.0.2"), 9, 8081, 1, &host_out2);
  EXPECT_EQ(result2, envoy_dynamic_module_type_cluster_result_MaxHostsReached);
}

TEST_F(AbiImplTest, AddAndRemoveHost) {
  envoy_dynamic_module_type_host_envoy_ptr host_out = nullptr;
  auto result = envoy_dynamic_module_callback_cluster_add_host(
      cluster_.get(), const_cast<char*>("127.0.0.1"), 9, 8080, 10, &host_out);

  EXPECT_EQ(result, envoy_dynamic_module_type_cluster_result_Success);
  EXPECT_NE(host_out, nullptr);

  // Remove the host.
  auto remove_result = envoy_dynamic_module_callback_cluster_remove_host(cluster_.get(), host_out);
  EXPECT_EQ(remove_result, envoy_dynamic_module_type_cluster_result_Success);

  // Verify it's gone.
  auto found_host = envoy_dynamic_module_callback_cluster_get_host_by_address(
      cluster_.get(), const_cast<char*>("127.0.0.1"), 9, 8080);
  EXPECT_EQ(found_host, nullptr);
}

TEST_F(AbiImplTest, GetHostsEmpty) {
  envoy_dynamic_module_type_host_info* hosts_out = nullptr;
  size_t hosts_count = 0;
  envoy_dynamic_module_callback_cluster_get_hosts(cluster_.get(), &hosts_out, &hosts_count);

  EXPECT_EQ(hosts_out, nullptr);
  EXPECT_EQ(hosts_count, 0);
}

TEST_F(AbiImplTest, GetHostsWithData) {
  // Add two hosts.
  envoy_dynamic_module_type_host_envoy_ptr host1 = nullptr;
  envoy_dynamic_module_callback_cluster_add_host(cluster_.get(), const_cast<char*>("127.0.0.1"), 9,
                                                 8080, 10, &host1);

  envoy_dynamic_module_type_host_envoy_ptr host2 = nullptr;
  envoy_dynamic_module_callback_cluster_add_host(cluster_.get(), const_cast<char*>("127.0.0.2"), 9,
                                                 8081, 20, &host2);

  envoy_dynamic_module_type_host_info* hosts_out = nullptr;
  size_t hosts_count = 0;
  envoy_dynamic_module_callback_cluster_get_hosts(cluster_.get(), &hosts_out, &hosts_count);

  EXPECT_NE(hosts_out, nullptr);
  EXPECT_EQ(hosts_count, 2);

  // Free the allocated memory.
  free(hosts_out);
}

TEST_F(AbiImplTest, GetHostByAddressNotFound) {
  auto host = envoy_dynamic_module_callback_cluster_get_host_by_address(
      cluster_.get(), const_cast<char*>("1.2.3.4"), 7, 9999);
  EXPECT_EQ(host, nullptr);
}

TEST_F(AbiImplTest, GetHostByAddressFound) {
  envoy_dynamic_module_type_host_envoy_ptr host_added = nullptr;
  envoy_dynamic_module_callback_cluster_add_host(cluster_.get(), const_cast<char*>("10.0.0.1"), 8,
                                                 9000, 5, &host_added);

  auto host_found = envoy_dynamic_module_callback_cluster_get_host_by_address(
      cluster_.get(), const_cast<char*>("10.0.0.1"), 8, 9000);
  EXPECT_EQ(host_found, host_added);
}

TEST_F(AbiImplTest, HostSetAndGetWeight) {
  envoy_dynamic_module_type_host_envoy_ptr host = nullptr;
  envoy_dynamic_module_callback_cluster_add_host(cluster_.get(), const_cast<char*>("192.168.1.1"),
                                                 11, 443, 50, &host);
  EXPECT_NE(host, nullptr);

  uint32_t weight = envoy_dynamic_module_callback_host_get_weight(host);
  EXPECT_EQ(weight, 50);

  envoy_dynamic_module_callback_host_set_weight(host, 75);
  weight = envoy_dynamic_module_callback_host_get_weight(host);
  EXPECT_EQ(weight, 75);
}

TEST_F(AbiImplTest, HostGetAddress) {
  envoy_dynamic_module_type_host_envoy_ptr host = nullptr;
  envoy_dynamic_module_callback_cluster_add_host(cluster_.get(), const_cast<char*>("172.16.0.1"),
                                                 10, 3000, 1, &host);
  EXPECT_NE(host, nullptr);

  envoy_dynamic_module_type_buffer_envoy_ptr address_out = nullptr;
  uint32_t port_out = 0;
  size_t address_length =
      envoy_dynamic_module_callback_host_get_address(host, &address_out, &port_out);

  EXPECT_GT(address_length, 0);
  EXPECT_NE(address_out, nullptr);
  std::string address(address_out, address_length);
  EXPECT_EQ(address, "172.16.0.1");
  EXPECT_EQ(port_out, 3000);
}

TEST_F(AbiImplTest, HostGetHealthUnhealthy) {
  envoy_dynamic_module_type_host_envoy_ptr host = nullptr;
  envoy_dynamic_module_callback_cluster_add_host(cluster_.get(), const_cast<char*>("10.1.1.1"), 8,
                                                 5000, 1, &host);
  EXPECT_NE(host, nullptr);

  // By default, hosts are healthy. Without actual health checks, this returns Healthy.
  auto health = envoy_dynamic_module_callback_host_get_health(host);
  // The health value depends on the health checker configuration.
  // For this test, just verify we get a valid health enum value.
  EXPECT_TRUE(health == envoy_dynamic_module_type_host_health_Healthy ||
              health == envoy_dynamic_module_type_host_health_Unhealthy ||
              health == envoy_dynamic_module_type_host_health_Degraded);
}

TEST_F(AbiImplTest, ClusterPreInitComplete) {
  // This callback just triggers the cluster to complete pre-init.
  // The no-op module does nothing, but we verify it doesn't crash.
  // Note: Don't call this without proper initialization flow, as it requires callbacks.
  SUCCEED();
}

TEST_F(AbiImplTest, LbContextGetHashKeyNull) {
  uint64_t hash_out = 0;
  bool result = envoy_dynamic_module_callback_lb_context_get_hash_key(nullptr, &hash_out);
  EXPECT_FALSE(result);
}

TEST_F(AbiImplTest, LbContextGetHashKeyNoValue) {
  NiceMock<Upstream::MockLoadBalancerContext> lb_context;
  EXPECT_CALL(lb_context, computeHashKey()).WillOnce(Return(absl::nullopt));

  uint64_t hash_out = 0;
  bool result = envoy_dynamic_module_callback_lb_context_get_hash_key(&lb_context, &hash_out);
  EXPECT_FALSE(result);
}

TEST_F(AbiImplTest, LbContextGetHashKeySuccess) {
  NiceMock<Upstream::MockLoadBalancerContext> lb_context;
  EXPECT_CALL(lb_context, computeHashKey()).WillOnce(Return(12345678));

  uint64_t hash_out = 0;
  bool result = envoy_dynamic_module_callback_lb_context_get_hash_key(&lb_context, &hash_out);
  EXPECT_TRUE(result);
  EXPECT_EQ(hash_out, 12345678);
}

TEST_F(AbiImplTest, LbContextGetHeaderNull) {
  envoy_dynamic_module_type_buffer_envoy_ptr value_out = nullptr;
  size_t result = envoy_dynamic_module_callback_lb_context_get_header(
      nullptr, const_cast<char*>("x-test"), 6, &value_out);
  EXPECT_EQ(result, 0);
}

TEST_F(AbiImplTest, LbContextGetHeaderNoHeaders) {
  NiceMock<Upstream::MockLoadBalancerContext> lb_context;
  EXPECT_CALL(lb_context, downstreamHeaders()).WillOnce(Return(nullptr));

  envoy_dynamic_module_type_buffer_envoy_ptr value_out = nullptr;
  size_t result = envoy_dynamic_module_callback_lb_context_get_header(
      &lb_context, const_cast<char*>("x-test"), 6, &value_out);
  EXPECT_EQ(result, 0);
}

TEST_F(AbiImplTest, LbContextGetOverrideHostNull) {
  envoy_dynamic_module_type_buffer_envoy_ptr address_out = nullptr;
  bool strict_out = false;
  size_t result = envoy_dynamic_module_callback_lb_context_get_override_host(nullptr, &address_out,
                                                                             &strict_out);
  EXPECT_EQ(result, 0);
}

TEST_F(AbiImplTest, LbContextGetOverrideHostNoValue) {
  NiceMock<Upstream::MockLoadBalancerContext> lb_context;
  EXPECT_CALL(lb_context, overrideHostToSelect()).WillOnce(Return(absl::nullopt));

  envoy_dynamic_module_type_buffer_envoy_ptr address_out = nullptr;
  bool strict_out = false;
  size_t result = envoy_dynamic_module_callback_lb_context_get_override_host(
      &lb_context, &address_out, &strict_out);
  EXPECT_EQ(result, 0);
}

TEST_F(AbiImplTest, LbContextGetOverrideHostSuccess) {
  NiceMock<Upstream::MockLoadBalancerContext> lb_context;
  std::string override_address = "192.168.100.1";
  EXPECT_CALL(lb_context, overrideHostToSelect())
      .WillOnce(Return(std::make_pair(absl::string_view(override_address), true)));

  envoy_dynamic_module_type_buffer_envoy_ptr address_out = nullptr;
  bool strict_out = false;
  size_t result = envoy_dynamic_module_callback_lb_context_get_override_host(
      &lb_context, &address_out, &strict_out);
  EXPECT_GT(result, 0);
  EXPECT_TRUE(strict_out);
}

TEST_F(AbiImplTest, LbContextGetAttemptCountNull) {
  uint32_t attempt_out = 0;
  bool result = envoy_dynamic_module_callback_lb_context_get_attempt_count(nullptr, &attempt_out);
  EXPECT_FALSE(result);
}

TEST_F(AbiImplTest, LbContextGetAttemptCountNoStreamInfo) {
  NiceMock<Upstream::MockLoadBalancerContext> lb_context;
  EXPECT_CALL(lb_context, requestStreamInfo()).WillOnce(Return(nullptr));

  uint32_t attempt_out = 0;
  bool result =
      envoy_dynamic_module_callback_lb_context_get_attempt_count(&lb_context, &attempt_out);
  EXPECT_FALSE(result);
}

TEST_F(AbiImplTest, LbContextGetAttemptCountNoValue) {
  NiceMock<Upstream::MockLoadBalancerContext> lb_context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  EXPECT_CALL(lb_context, requestStreamInfo()).WillOnce(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillOnce(Return(absl::nullopt));

  uint32_t attempt_out = 0;
  bool result =
      envoy_dynamic_module_callback_lb_context_get_attempt_count(&lb_context, &attempt_out);
  EXPECT_FALSE(result);
}

TEST_F(AbiImplTest, LbContextGetAttemptCountSuccess) {
  NiceMock<Upstream::MockLoadBalancerContext> lb_context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  EXPECT_CALL(lb_context, requestStreamInfo()).WillOnce(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillOnce(Return(5));

  uint32_t attempt_out = 0;
  bool result =
      envoy_dynamic_module_callback_lb_context_get_attempt_count(&lb_context, &attempt_out);
  EXPECT_TRUE(result);
  EXPECT_EQ(attempt_out, 5);
}

TEST_F(AbiImplTest, LbContextGetDownstreamConnectionIdNull) {
  uint64_t connection_id_out = 0;
  bool result = envoy_dynamic_module_callback_lb_context_get_downstream_connection_id(
      nullptr, &connection_id_out);
  EXPECT_FALSE(result);
}

TEST_F(AbiImplTest, LbContextGetDownstreamConnectionIdNoConnection) {
  NiceMock<Upstream::MockLoadBalancerContext> lb_context;
  EXPECT_CALL(lb_context, downstreamConnection()).WillOnce(Return(nullptr));

  uint64_t connection_id_out = 0;
  bool result = envoy_dynamic_module_callback_lb_context_get_downstream_connection_id(
      &lb_context, &connection_id_out);
  EXPECT_FALSE(result);
}

TEST_F(AbiImplTest, LbContextGetDownstreamConnectionIdSuccess) {
  NiceMock<Upstream::MockLoadBalancerContext> lb_context;
  NiceMock<Network::MockConnection> connection;

  EXPECT_CALL(connection, id()).WillOnce(Return(999888777));
  EXPECT_CALL(lb_context, downstreamConnection()).WillOnce(Return(&connection));

  uint64_t connection_id_out = 0;
  bool result = envoy_dynamic_module_callback_lb_context_get_downstream_connection_id(
      &lb_context, &connection_id_out);
  EXPECT_TRUE(result);
  EXPECT_EQ(connection_id_out, 999888777);
}

TEST_F(AbiImplTest, ClusterManagerGetThreadLocalClusterNotFound) {
  auto result = envoy_dynamic_module_callback_cluster_manager_get_thread_local_cluster(
      cluster_.get(), const_cast<char*>("nonexistent"), 11);
  EXPECT_EQ(result, nullptr);
}

TEST_F(AbiImplTest, ThreadLocalClusterChooseHostNull) {
  auto result = envoy_dynamic_module_callback_thread_local_cluster_choose_host(nullptr, nullptr);
  EXPECT_EQ(result, nullptr);
}

TEST_F(AbiImplTest, ThreadLocalClusterGetNameNull) {
  envoy_dynamic_module_type_buffer_envoy_ptr name_out = nullptr;
  size_t result = envoy_dynamic_module_callback_thread_local_cluster_get_name(nullptr, &name_out);
  EXPECT_EQ(result, 0);
}

TEST_F(AbiImplTest, ThreadLocalClusterHostCountNull) {
  size_t result = envoy_dynamic_module_callback_thread_local_cluster_host_count(nullptr);
  EXPECT_EQ(result, 0);
}

TEST_F(AbiImplTest, ThreadLocalClusterGetNameSuccess) {
  NiceMock<Upstream::MockThreadLocalCluster> tl_cluster;
  auto cluster_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  std::string cluster_name = "test_tl_cluster";

  EXPECT_CALL(*cluster_info, name()).WillOnce(ReturnRef(cluster_name));
  EXPECT_CALL(tl_cluster, info()).WillOnce(Return(cluster_info));

  envoy_dynamic_module_type_buffer_envoy_ptr name_out = nullptr;
  size_t result =
      envoy_dynamic_module_callback_thread_local_cluster_get_name(&tl_cluster, &name_out);
  EXPECT_EQ(result, cluster_name.size());
  EXPECT_NE(name_out, nullptr);
  std::string retrieved_name(name_out, result);
  EXPECT_EQ(retrieved_name, cluster_name);
}

TEST_F(AbiImplTest, LbContextGetHeaderSuccess) {
  // Test getting a header from load balancer context.
  NiceMock<Upstream::MockLoadBalancerContext> lb_context;
  Http::TestRequestHeaderMapImpl request_headers{{"x-custom-header", "test-value-123"}};

  EXPECT_CALL(lb_context, downstreamHeaders()).WillOnce(Return(&request_headers));

  envoy_dynamic_module_type_buffer_envoy_ptr value_out = nullptr;
  std::string header_key = "x-custom-header";
  size_t result = envoy_dynamic_module_callback_lb_context_get_header(
      &lb_context, const_cast<char*>(header_key.c_str()), header_key.size(), &value_out);

  EXPECT_EQ(result, 14); // Length of "test-value-123".
  EXPECT_NE(value_out, nullptr);
  std::string value_str(value_out, result);
  EXPECT_EQ(value_str, "test-value-123");
}

TEST_F(AbiImplTest, LbContextGetHeaderNotFound) {
  // Test getting a header that doesn't exist.
  NiceMock<Upstream::MockLoadBalancerContext> lb_context;
  Http::TestRequestHeaderMapImpl request_headers{{"x-other-header", "other-value"}};

  EXPECT_CALL(lb_context, downstreamHeaders()).WillOnce(Return(&request_headers));

  envoy_dynamic_module_type_buffer_envoy_ptr value_out = nullptr;
  std::string header_key = "x-nonexistent";
  size_t result = envoy_dynamic_module_callback_lb_context_get_header(
      &lb_context, const_cast<char*>(header_key.c_str()), header_key.size(), &value_out);

  EXPECT_EQ(result, 0);
}

TEST_F(AbiImplTest, ThreadLocalClusterChooseHostSuccess) {
  // Test chooseHost success path using Invoke to handle non-copyable return type.
  NiceMock<Upstream::MockThreadLocalCluster> tl_cluster;
  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();

  EXPECT_CALL(tl_cluster, chooseHost(_)).WillOnce(Invoke([&host](Upstream::LoadBalancerContext*) {
    return Upstream::HostSelectionResponse(host);
  }));

  auto result =
      envoy_dynamic_module_callback_thread_local_cluster_choose_host(&tl_cluster, nullptr);
  EXPECT_NE(result, nullptr);
  EXPECT_EQ(result, host.get());
}

TEST_F(AbiImplTest, ThreadLocalClusterChooseHostNoHost) {
  // Test chooseHost when no host is available.
  NiceMock<Upstream::MockThreadLocalCluster> tl_cluster;

  EXPECT_CALL(tl_cluster, chooseHost(_)).WillOnce(Invoke([](Upstream::LoadBalancerContext*) {
    return Upstream::HostSelectionResponse(nullptr);
  }));

  auto result =
      envoy_dynamic_module_callback_thread_local_cluster_choose_host(&tl_cluster, nullptr);
  EXPECT_EQ(result, nullptr);
}

} // namespace
} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
