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

} // namespace
} // namespace Server
} // namespace Envoy
