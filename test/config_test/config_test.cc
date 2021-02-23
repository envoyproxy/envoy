#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"

#include "common/common/fmt.h"
#include "common/protobuf/utility.h"
#include "common/runtime/runtime_features.h"

#include "exe/main_common.h"

#include "server/config_validation/server.h"
#include "server/configuration_impl.h"
#include "server/options_impl.h"

#include "test/integration/server.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace ConfigTest {
namespace {

static std::vector<absl::string_view> unsuported_win32_configs = {
#if defined(WIN32) && !defined(SO_ORIGINAL_DST)
    "configs_original-dst-cluster_proxy_config.yaml"
#endif
};

} // namespace

void testMerge() {
  Api::ApiPtr api = Api::createApiForTest();

  const std::string overlay = "static_resources: { clusters: [{name: 'foo'}]}";
  OptionsImpl options(Server::createTestOptionsImpl("envoyproxy_io_proxy.yaml", overlay,
                                                    Network::Address::IpVersion::v6));
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  Server::InstanceUtil::loadBootstrapConfig(bootstrap, options,
                                            ProtobufMessage::getStrictValidationVisitor(), *api);
  EXPECT_EQ(2, bootstrap.static_resources().clusters_size());
}

uint32_t run(const std::string& directory) {
  uint32_t num_tested = 0;
  Api::ApiPtr api = Api::createApiForTest();
  for (const std::string& filename : TestUtility::listFiles(directory, false)) {
    ENVOY_LOG_MISC(info, "testing {}.\n", filename);
    if (std::find_if(unsuported_win32_configs.begin(), unsuported_win32_configs.end(),
                     [filename](const absl::string_view& s) {
                       return filename.find(s) != std::string::npos;
                     }) == unsuported_win32_configs.end()) {
      OptionsImpl options(
          Envoy::Server::createTestOptionsImpl(filename, "", Network::Address::IpVersion::v6));
      options.setMode(Server::Mode::Validate);
      options.setLogLevel(spdlog::level::trace);

      PlatformImpl platform_impl;
      Event::SimulatedTimeSystem real_time_system;
      DefaultListenerHooks default_listener_hooks;
      ProdComponentFactory prod_component_factory;

      try {
        MainCommonBase base(options, real_time_system, default_listener_hooks,
                            prod_component_factory, std::make_unique<Random::RandomGeneratorImpl>(),
                            platform_impl.threadFactory(), platform_impl.fileSystem(), nullptr);
      } catch (const EnvoyException& ex) {
        ADD_FAILURE() << fmt::format("'{}' config failed. Error: {}", filename, ex.what());
      }
    }
    num_tested++;
  }
  return num_tested;
}

void loadVersionedBootstrapFile(const std::string& filename,
                                envoy::config::bootstrap::v3::Bootstrap& bootstrap_message,
                                absl::optional<uint32_t> bootstrap_version) {
  Api::ApiPtr api = Api::createApiForTest();
  OptionsImpl options(
      Envoy::Server::createTestOptionsImpl(filename, "", Network::Address::IpVersion::v6));
  // Avoid contention issues with other tests over the hot restart domain socket.
  options.setHotRestartDisabled(true);
  if (bootstrap_version.has_value()) {
    options.setBootstrapVersion(*bootstrap_version);
  }
  Server::InstanceUtil::loadBootstrapConfig(bootstrap_message, options,
                                            ProtobufMessage::getStrictValidationVisitor(), *api);
}

void loadBootstrapConfigProto(const envoy::config::bootstrap::v3::Bootstrap& in_proto,
                              envoy::config::bootstrap::v3::Bootstrap& bootstrap_message) {
  Api::ApiPtr api = Api::createApiForTest();
  OptionsImpl options(
      Envoy::Server::createTestOptionsImpl("", "", Network::Address::IpVersion::v6));
  options.setConfigProto(in_proto);
  // Avoid contention issues with other tests over the hot restart domain socket.
  options.setHotRestartDisabled(true);
  Server::InstanceUtil::loadBootstrapConfig(bootstrap_message, options,
                                            ProtobufMessage::getStrictValidationVisitor(), *api);
}

} // namespace ConfigTest
} // namespace Envoy
