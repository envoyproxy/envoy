#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/common/protobuf/visitor.h"
#include "source/server/options_impl_base.h"
#include "source/server/server.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

TEST(BootstrapConfigDirectoryTest, LoadsAndMergesYamlFilesInLexicalOrder) {
  Api::ApiPtr api = Api::createApiForTest();

  const std::string config_dir = TestEnvironment::temporaryPath("bootstrap_config_dir_merge");
  TestEnvironment::removePath(config_dir);
  TestEnvironment::createPath(config_dir);

  TestEnvironment::writeStringToFileForTest(absl::StrCat(config_dir, "/00-base.yaml"),
                                            R"EOF(
node:
  id: base-id
  cluster: base-cluster
admin:
  access_log_path: /tmp/admin_access.log
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF",
                                            /*fully_qualified_path=*/true);

  TestEnvironment::writeStringToFileForTest(absl::StrCat(config_dir, "/10-static.yaml"),
                                            R"EOF(
static_resources:
  clusters:
  - name: service
    type: STATIC
    connect_timeout: 0.25s
    load_assignment:
      cluster_name: service
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 80
)EOF",
                                            /*fully_qualified_path=*/true);

  TestEnvironment::writeStringToFileForTest(absl::StrCat(config_dir, "/20-override.yaml"),
                                            R"EOF(
node:
  id: override-id
static_resources:
  clusters:
  - name: service2
    type: STATIC
    connect_timeout: 0.25s
    load_assignment:
      cluster_name: service2
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 81
)EOF",
                                            /*fully_qualified_path=*/true);

  OptionsImplBase options;
  options.setConfigPath(config_dir);
  options.setLocalAddressIpVersion(Network::Address::IpVersion::v4);

  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  ASSERT_TRUE(InstanceUtil::loadBootstrapConfig(bootstrap, options,
                                                ProtobufMessage::getStrictValidationVisitor(), *api)
                  .ok());
  EXPECT_EQ("override-id", bootstrap.node().id());
  ASSERT_EQ(2, bootstrap.static_resources().clusters_size());
  EXPECT_EQ("service", bootstrap.static_resources().clusters(0).name());
  EXPECT_EQ("service2", bootstrap.static_resources().clusters(1).name());

  TestEnvironment::removePath(config_dir);
}

TEST(BootstrapConfigDirectoryTest, EmptyDirectoryIsRejected) {
  Api::ApiPtr api = Api::createApiForTest();

  const std::string empty_dir = TestEnvironment::temporaryPath("bootstrap_config_dir_empty");
  TestEnvironment::removePath(empty_dir);
  TestEnvironment::createPath(empty_dir);

  OptionsImplBase options;
  options.setConfigPath(empty_dir);
  options.setLocalAddressIpVersion(Network::Address::IpVersion::v4);

  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  EXPECT_FALSE(InstanceUtil::loadBootstrapConfig(
                   bootstrap, options, ProtobufMessage::getStrictValidationVisitor(), *api)
                   .ok());

  TestEnvironment::removePath(empty_dir);
}

TEST(BootstrapConfigDirectoryTest, IncludeGlobLoadsFragmentsAndMainFileOverridesThem) {
  Api::ApiPtr api = Api::createApiForTest();

  const std::string config_root = TestEnvironment::temporaryPath("bootstrap_config_include_glob");
  TestEnvironment::removePath(config_root);
  TestEnvironment::createPath(config_root);
  TestEnvironment::createPath(absl::StrCat(config_root, "/conf.d"));

  TestEnvironment::writeStringToFileForTest(absl::StrCat(config_root, "/conf.d/00-base.yaml"),
                                            R"EOF(
node:
  id: included-id
  cluster: included-cluster
admin:
  access_log_path: /tmp/admin_access.log
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF",
                                            /*fully_qualified_path=*/true);

  TestEnvironment::writeStringToFileForTest(absl::StrCat(config_root, "/conf.d/10-cluster.yaml"),
                                            R"EOF(
static_resources:
  clusters:
  - name: service
    type: STATIC
    connect_timeout: 0.25s
    load_assignment:
      cluster_name: service
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 80
)EOF",
                                            /*fully_qualified_path=*/true);

  const std::string root_config_path =
      TestEnvironment::writeStringToFileForTest(absl::StrCat(config_root, "/bootstrap.yaml"),
                                                R"EOF(
include: conf.d/*.yaml
node:
  id: root-id
)EOF",
                                                /*fully_qualified_path=*/true);

  OptionsImplBase options;
  options.setConfigPath(root_config_path);
  options.setLocalAddressIpVersion(Network::Address::IpVersion::v4);

  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  ASSERT_TRUE(InstanceUtil::loadBootstrapConfig(bootstrap, options,
                                                ProtobufMessage::getStrictValidationVisitor(), *api)
                  .ok());
  EXPECT_EQ("root-id", bootstrap.node().id());
  EXPECT_EQ("included-cluster", bootstrap.node().cluster());
  ASSERT_EQ(1, bootstrap.static_resources().clusters_size());
  EXPECT_EQ("service", bootstrap.static_resources().clusters(0).name());

  TestEnvironment::removePath(config_root);
}

TEST(BootstrapConfigDirectoryTest, IncludeCycleIsRejected) {
  Api::ApiPtr api = Api::createApiForTest();

  const std::string config_root = TestEnvironment::temporaryPath("bootstrap_config_include_cycle");
  TestEnvironment::removePath(config_root);
  TestEnvironment::createPath(config_root);

  const std::string config_a_path =
      TestEnvironment::writeStringToFileForTest(absl::StrCat(config_root, "/a.yaml"),
                                                R"EOF(
include: b.yaml
)EOF",
                                                /*fully_qualified_path=*/true);

  TestEnvironment::writeStringToFileForTest(absl::StrCat(config_root, "/b.yaml"),
                                            R"EOF(
include: ./a.yaml
)EOF",
                                            /*fully_qualified_path=*/true);

  OptionsImplBase options;
  options.setConfigPath(config_a_path);
  options.setLocalAddressIpVersion(Network::Address::IpVersion::v4);

  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  EXPECT_FALSE(InstanceUtil::loadBootstrapConfig(
                   bootstrap, options, ProtobufMessage::getStrictValidationVisitor(), *api)
                   .ok());

  TestEnvironment::removePath(config_root);
}

TEST(BootstrapConfigDirectoryTest, IncludeNestedPathWithinBaseDirectoryIsAllowed) {
  Api::ApiPtr api = Api::createApiForTest();

  const std::string config_root =
      TestEnvironment::temporaryPath("bootstrap_config_include_nested_path");
  TestEnvironment::removePath(config_root);
  TestEnvironment::createPath(config_root);
  TestEnvironment::createPath(absl::StrCat(config_root, "/includes"));

  TestEnvironment::writeStringToFileForTest(absl::StrCat(config_root, "/includes/cluster.yaml"),
                                            R"EOF(
static_resources:
  clusters:
  - name: nested-cluster
    type: STATIC
    connect_timeout: 0.25s
    load_assignment:
      cluster_name: nested-cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 80
)EOF",
                                            /*fully_qualified_path=*/true);

  const std::string config_path =
      TestEnvironment::writeStringToFileForTest(absl::StrCat(config_root, "/bootstrap.yaml"),
                                                R"EOF(
include: includes/cluster.yaml
node:
  id: nested-id
  cluster: nested-cluster
admin:
  access_log_path: /tmp/admin_access.log
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF",
                                                /*fully_qualified_path=*/true);

  OptionsImplBase options;
  options.setConfigPath(config_path);
  options.setLocalAddressIpVersion(Network::Address::IpVersion::v4);

  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  const absl::Status status = InstanceUtil::loadBootstrapConfig(
      bootstrap, options, ProtobufMessage::getStrictValidationVisitor(), *api);
  ASSERT_TRUE(status.ok()) << status;
  ASSERT_EQ(1, bootstrap.static_resources().clusters_size());
  EXPECT_EQ("nested-cluster", bootstrap.static_resources().clusters(0).name());

  TestEnvironment::removePath(config_root);
}

TEST(BootstrapConfigDirectoryTest, IncludeOutsideBaseDirectoryIsRejected) {
  Api::ApiPtr api = Api::createApiForTest();

  const std::string parent_dir =
      TestEnvironment::temporaryPath("bootstrap_config_include_outside_parent");
  const std::string config_root = absl::StrCat(parent_dir, "/config");
  TestEnvironment::removePath(parent_dir);
  TestEnvironment::createPath(config_root);

  TestEnvironment::writeStringToFileForTest(absl::StrCat(parent_dir, "/outside.yaml"),
                                            R"EOF(
static_resources:
  clusters:
  - name: outside-cluster
    type: STATIC
    connect_timeout: 0.25s
    load_assignment:
      cluster_name: outside-cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 80
)EOF",
                                            /*fully_qualified_path=*/true);

  const std::string config_path =
      TestEnvironment::writeStringToFileForTest(absl::StrCat(config_root, "/bootstrap.yaml"),
                                                R"EOF(
include: ../outside.yaml
node:
  id: root-id
  cluster: root-cluster
admin:
  access_log_path: /tmp/admin_access.log
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF",
                                                /*fully_qualified_path=*/true);

  OptionsImplBase options;
  options.setConfigPath(config_path);
  options.setLocalAddressIpVersion(Network::Address::IpVersion::v4);

  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  const absl::Status status = InstanceUtil::loadBootstrapConfig(
      bootstrap, options, ProtobufMessage::getStrictValidationVisitor(), *api);
  EXPECT_FALSE(status.ok());
  EXPECT_NE(std::string::npos, status.message().find("resolves outside base directory"));

  TestEnvironment::removePath(parent_dir);
}

} // namespace
} // namespace Server
} // namespace Envoy
