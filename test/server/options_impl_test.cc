#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"

#include "common/common/utility.h"

#include "server/options_impl.h"

#if defined(__linux__)
#include <sched.h>
#include "server/options_impl_platform_linux.h"
#endif
#include "test/mocks/api/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

using testing::HasSubstr;

namespace Envoy {
namespace {

class OptionsImplTest : public testing::Test {

public:
  // Do the ugly work of turning a std::string into a char** and create an OptionsImpl. Args are
  // separated by a single space: no fancy quoting or escaping.
  std::unique_ptr<OptionsImpl> createOptionsImpl(const std::string& args) {
    std::vector<std::string> words = TestUtility::split(args, ' ');
    std::vector<const char*> argv;
    std::transform(words.cbegin(), words.cend(), std::back_inserter(argv),
                   [](const std::string& arg) { return arg.c_str(); });
    return std::make_unique<OptionsImpl>(
        argv.size(), argv.data(), [](bool) { return "1"; }, spdlog::level::warn);
  }
};

TEST_F(OptionsImplTest, HotRestartVersion) {
  EXPECT_THROW_WITH_REGEX(createOptionsImpl("envoy --hot-restart-version"), NoServingException,
                          "NoServingException");
}

TEST_F(OptionsImplTest, InvalidMode) {
  EXPECT_THROW_WITH_REGEX(createOptionsImpl("envoy --mode bogus"), MalformedArgvException, "bogus");
}

TEST_F(OptionsImplTest, InvalidCommandLine) {
  EXPECT_THROW_WITH_REGEX(createOptionsImpl("envoy --blah"), MalformedArgvException,
                          "Couldn't find match for argument");
}

TEST_F(OptionsImplTest, v1Disallowed) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl(
      "envoy --mode validate --concurrency 2 -c hello --admin-address-path path --restart-epoch 1 "
      "--local-address-ip-version v6 -l info --service-cluster cluster --service-node node "
      "--service-zone zone --file-flush-interval-msec 9000 --drain-time-s 60 --log-format [%v] "
      "--parent-shutdown-time-s 90 --log-path /foo/bar --disable-hot-restart");
  EXPECT_EQ(Server::Mode::Validate, options->mode());
}

TEST_F(OptionsImplTest, All) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl(
      "envoy --mode validate --concurrency 2 -c hello --admin-address-path path --restart-epoch 1 "
      "--local-address-ip-version v6 -l info --component-log-level upstream:debug,connection:trace "
      "--service-cluster cluster --service-node node --service-zone zone "
      "--file-flush-interval-msec 9000 "
      "--drain-time-s 60 --log-format [%v] --parent-shutdown-time-s 90 --log-path /foo/bar "
      "--disable-hot-restart --cpuset-threads --allow-unknown-static-fields "
      "--reject-unknown-dynamic-fields");
  EXPECT_EQ(Server::Mode::Validate, options->mode());
  EXPECT_EQ(2U, options->concurrency());
  EXPECT_EQ("hello", options->configPath());
  EXPECT_EQ("path", options->adminAddressPath());
  EXPECT_EQ(Network::Address::IpVersion::v6, options->localAddressIpVersion());
  EXPECT_EQ(1U, options->restartEpoch());
  EXPECT_EQ(spdlog::level::info, options->logLevel());
  EXPECT_EQ(2, options->componentLogLevels().size());
  EXPECT_EQ("[%v]", options->logFormat());
  EXPECT_EQ("/foo/bar", options->logPath());
  EXPECT_EQ("cluster", options->serviceClusterName());
  EXPECT_EQ("node", options->serviceNodeName());
  EXPECT_EQ("zone", options->serviceZone());
  EXPECT_EQ(std::chrono::milliseconds(9000), options->fileFlushIntervalMsec());
  EXPECT_EQ(std::chrono::seconds(60), options->drainTime());
  EXPECT_EQ(std::chrono::seconds(90), options->parentShutdownTime());
  EXPECT_TRUE(options->hotRestartDisabled());
  EXPECT_FALSE(options->libeventBufferEnabled());
  EXPECT_TRUE(options->cpusetThreadsEnabled());
  EXPECT_TRUE(options->allowUnknownStaticFields());
  EXPECT_TRUE(options->rejectUnknownDynamicFields());

  options = createOptionsImpl("envoy --mode init_only");
  EXPECT_EQ(Server::Mode::InitOnly, options->mode());
}

// Either variants of allow-unknown-[static-]-fields works.
TEST_F(OptionsImplTest, AllowUnknownFields) {
  {
    std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy");
    EXPECT_FALSE(options->allowUnknownStaticFields());
  }
  {
    std::unique_ptr<OptionsImpl> options;
    EXPECT_LOG_CONTAINS(
        "warning",
        "--allow-unknown-fields is deprecated, use --allow-unknown-static-fields instead.",
        options = createOptionsImpl("envoy --allow-unknown-fields"));
    EXPECT_TRUE(options->allowUnknownStaticFields());
  }
  {
    std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy --allow-unknown-static-fields");
    EXPECT_TRUE(options->allowUnknownStaticFields());
  }
}

TEST_F(OptionsImplTest, SetAll) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy -c hello");
  bool hot_restart_disabled = options->hotRestartDisabled();
  bool signal_handling_enabled = options->signalHandlingEnabled();
  bool cpuset_threads_enabled = options->cpusetThreadsEnabled();

  options->setBaseId(109876);
  options->setConcurrency(42);
  options->setConfigPath("foo");
  envoy::config::bootstrap::v2::Bootstrap bootstrap_foo{};
  bootstrap_foo.mutable_node()->set_id("foo");
  options->setConfigProto(bootstrap_foo);
  options->setConfigYaml("bogus:");
  options->setAdminAddressPath("path");
  options->setLocalAddressIpVersion(Network::Address::IpVersion::v6);
  options->setDrainTime(std::chrono::seconds(42));
  options->setLogLevel(spdlog::level::trace);
  options->setLogFormat("%L %n %v");
  options->setLogPath("/foo/bar");
  options->setParentShutdownTime(std::chrono::seconds(43));
  options->setRestartEpoch(44);
  options->setFileFlushIntervalMsec(std::chrono::milliseconds(45));
  options->setMode(Server::Mode::Validate);
  options->setServiceClusterName("cluster_foo");
  options->setServiceNodeName("node_foo");
  options->setServiceZone("zone_foo");
  options->setHotRestartDisabled(!options->hotRestartDisabled());
  options->setSignalHandling(!options->signalHandlingEnabled());
  options->setCpusetThreads(!options->cpusetThreadsEnabled());
  options->setAllowUnkownFields(true);
  options->setRejectUnknownFieldsDynamic(true);

  EXPECT_EQ(109876, options->baseId());
  EXPECT_EQ(42U, options->concurrency());
  EXPECT_EQ("foo", options->configPath());
  envoy::config::bootstrap::v2::Bootstrap bootstrap_bar{};
  bootstrap_bar.mutable_node()->set_id("foo");
  EXPECT_TRUE(TestUtility::protoEqual(bootstrap_bar, options->configProto()));
  EXPECT_EQ("bogus:", options->configYaml());
  EXPECT_EQ("path", options->adminAddressPath());
  EXPECT_EQ(Network::Address::IpVersion::v6, options->localAddressIpVersion());
  EXPECT_EQ(std::chrono::seconds(42), options->drainTime());
  EXPECT_EQ(spdlog::level::trace, options->logLevel());
  EXPECT_EQ("%L %n %v", options->logFormat());
  EXPECT_EQ("/foo/bar", options->logPath());
  EXPECT_EQ(std::chrono::seconds(43), options->parentShutdownTime());
  EXPECT_EQ(44, options->restartEpoch());
  EXPECT_EQ(std::chrono::milliseconds(45), options->fileFlushIntervalMsec());
  EXPECT_EQ(Server::Mode::Validate, options->mode());
  EXPECT_EQ("cluster_foo", options->serviceClusterName());
  EXPECT_EQ("node_foo", options->serviceNodeName());
  EXPECT_EQ("zone_foo", options->serviceZone());
  EXPECT_EQ(!hot_restart_disabled, options->hotRestartDisabled());
  EXPECT_EQ(!signal_handling_enabled, options->signalHandlingEnabled());
  EXPECT_EQ(!cpuset_threads_enabled, options->cpusetThreadsEnabled());
  EXPECT_TRUE(options->allowUnknownStaticFields());
  EXPECT_TRUE(options->rejectUnknownDynamicFields());

  // Validate that CommandLineOptions is constructed correctly.
  Server::CommandLineOptionsPtr command_line_options = options->toCommandLineOptions();

  EXPECT_EQ(options->baseId(), command_line_options->base_id());
  EXPECT_EQ(options->concurrency(), command_line_options->concurrency());
  EXPECT_EQ(options->configPath(), command_line_options->config_path());
  EXPECT_EQ(options->configYaml(), command_line_options->config_yaml());
  EXPECT_EQ(options->adminAddressPath(), command_line_options->admin_address_path());
  EXPECT_EQ(envoy::admin::v2alpha::CommandLineOptions::v6,
            command_line_options->local_address_ip_version());
  EXPECT_EQ(options->drainTime().count(), command_line_options->drain_time().seconds());
  EXPECT_EQ(spdlog::level::to_string_view(options->logLevel()), command_line_options->log_level());
  EXPECT_EQ(options->logFormat(), command_line_options->log_format());
  EXPECT_EQ(options->logPath(), command_line_options->log_path());
  EXPECT_EQ(options->parentShutdownTime().count(),
            command_line_options->parent_shutdown_time().seconds());
  EXPECT_EQ(options->restartEpoch(), command_line_options->restart_epoch());
  EXPECT_EQ(options->fileFlushIntervalMsec().count() / 1000,
            command_line_options->file_flush_interval().seconds());
  EXPECT_EQ(envoy::admin::v2alpha::CommandLineOptions::Validate, command_line_options->mode());
  EXPECT_EQ(options->serviceClusterName(), command_line_options->service_cluster());
  EXPECT_EQ(options->serviceNodeName(), command_line_options->service_node());
  EXPECT_EQ(options->serviceZone(), command_line_options->service_zone());
  EXPECT_EQ(options->hotRestartDisabled(), command_line_options->disable_hot_restart());
  EXPECT_EQ(options->mutexTracingEnabled(), command_line_options->enable_mutex_tracing());
  EXPECT_EQ(options->cpusetThreadsEnabled(), command_line_options->cpuset_threads());
}

TEST_F(OptionsImplTest, DefaultParams) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy -c hello");
  EXPECT_EQ(std::chrono::seconds(600), options->drainTime());
  EXPECT_EQ(std::chrono::seconds(900), options->parentShutdownTime());
  EXPECT_EQ("", options->adminAddressPath());
  EXPECT_EQ(Network::Address::IpVersion::v4, options->localAddressIpVersion());
  EXPECT_EQ(Server::Mode::Serve, options->mode());
  EXPECT_FALSE(options->hotRestartDisabled());
  EXPECT_FALSE(options->cpusetThreadsEnabled());

  // Validate that CommandLineOptions is constructed correctly with default params.
  Server::CommandLineOptionsPtr command_line_options = options->toCommandLineOptions();

  EXPECT_EQ(600, command_line_options->drain_time().seconds());
  EXPECT_EQ(900, command_line_options->parent_shutdown_time().seconds());
  EXPECT_EQ("", command_line_options->admin_address_path());
  EXPECT_EQ(envoy::admin::v2alpha::CommandLineOptions::v4,
            command_line_options->local_address_ip_version());
  EXPECT_EQ(envoy::admin::v2alpha::CommandLineOptions::Serve, command_line_options->mode());
  EXPECT_FALSE(command_line_options->disable_hot_restart());
  EXPECT_FALSE(command_line_options->cpuset_threads());
  EXPECT_FALSE(command_line_options->allow_unknown_static_fields());
  EXPECT_FALSE(command_line_options->reject_unknown_dynamic_fields());
}

// Validates that the server_info proto is in sync with the options.
TEST_F(OptionsImplTest, OptionsAreInSyncWithProto) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy -c hello");
  Server::CommandLineOptionsPtr command_line_options = options->toCommandLineOptions();
  // Failure of this condition indicates that the server_info proto is not in sync with the options.
  // If an option is added/removed, please update server_info proto as well to keep it in sync.
  // Currently the following 5 options are not defined in proto, hence the count differs by 5.
  // 1. version        - default TCLAP argument.
  // 2. help           - default TCLAP argument.
  // 3. ignore_rest    - default TCLAP argument.
  // 4. use-libevent-buffers  - short-term override for rollout of new buffer implementation.
  // 5. allow-unknown-fields  - deprecated alias of allow-unknown-static-fields.
  EXPECT_EQ(options->count() - 5, command_line_options->GetDescriptor()->field_count());
}

TEST_F(OptionsImplTest, BadCliOption) {
  EXPECT_THROW_WITH_REGEX(createOptionsImpl("envoy -c hello --local-address-ip-version foo"),
                          MalformedArgvException, "error: unknown IP address version 'foo'");
}

TEST_F(OptionsImplTest, ParseComponentLogLevels) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy --mode init_only");
  options->parseComponentLogLevels("upstream:debug,connection:trace");
  const std::vector<std::pair<std::string, spdlog::level::level_enum>>& component_log_levels =
      options->componentLogLevels();
  EXPECT_EQ(2, component_log_levels.size());
  EXPECT_EQ("upstream", component_log_levels[0].first);
  EXPECT_EQ(spdlog::level::level_enum::debug, component_log_levels[0].second);
  EXPECT_EQ("connection", component_log_levels[1].first);
  EXPECT_EQ(spdlog::level::level_enum::trace, component_log_levels[1].second);
}

TEST_F(OptionsImplTest, ParseComponentLogLevelsWithBlank) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy --mode init_only");
  options->parseComponentLogLevels("");
  EXPECT_EQ(0, options->componentLogLevels().size());
}

TEST_F(OptionsImplTest, InvalidComponent) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy --mode init_only");
  EXPECT_THROW_WITH_REGEX(options->parseComponentLogLevels("blah:debug"), MalformedArgvException,
                          "error: invalid component specified 'blah'");
}

TEST_F(OptionsImplTest, InvalidLogLevel) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy --mode init_only");
  EXPECT_THROW_WITH_REGEX(options->parseComponentLogLevels("upstream:blah,connection:trace"),
                          MalformedArgvException, "error: invalid log level specified 'blah'");
}

TEST_F(OptionsImplTest, InvalidComponentLogLevelStructure) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy --mode init_only");
  EXPECT_THROW_WITH_REGEX(options->parseComponentLogLevels("upstream:foo:bar"),
                          MalformedArgvException,
                          "error: component log level not correctly specified 'upstream:foo:bar'");
}

TEST_F(OptionsImplTest, IncompleteComponentLogLevel) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy --mode init_only");
  EXPECT_THROW_WITH_REGEX(options->parseComponentLogLevels("upstream"), MalformedArgvException,
                          "component log level not correctly specified 'upstream'");
}

// Test that the test constructor comes up with the same default values as the main constructor.
TEST_F(OptionsImplTest, SaneTestConstructor) {
  std::unique_ptr<OptionsImpl> regular_options_impl(createOptionsImpl("envoy"));
  OptionsImpl test_options_impl("service_cluster", "service_node", "service_zone",
                                spdlog::level::level_enum::info);

  // Specified by constructor
  EXPECT_EQ("service_cluster", test_options_impl.serviceClusterName());
  EXPECT_EQ("service_node", test_options_impl.serviceNodeName());
  EXPECT_EQ("service_zone", test_options_impl.serviceZone());
  EXPECT_EQ(spdlog::level::level_enum::info, test_options_impl.logLevel());

  // Special (simplified) for tests
  EXPECT_EQ(1u, test_options_impl.concurrency());

  EXPECT_EQ(regular_options_impl->baseId(), test_options_impl.baseId());
  EXPECT_EQ(regular_options_impl->configPath(), test_options_impl.configPath());
  EXPECT_TRUE(TestUtility::protoEqual(regular_options_impl->configProto(),
                                      test_options_impl.configProto()));
  EXPECT_EQ(regular_options_impl->configYaml(), test_options_impl.configYaml());
  EXPECT_EQ(regular_options_impl->adminAddressPath(), test_options_impl.adminAddressPath());
  EXPECT_EQ(regular_options_impl->localAddressIpVersion(),
            test_options_impl.localAddressIpVersion());
  EXPECT_EQ(regular_options_impl->drainTime(), test_options_impl.drainTime());
  EXPECT_EQ(spdlog::level::level_enum::info, test_options_impl.logLevel());
  EXPECT_EQ(regular_options_impl->componentLogLevels(), test_options_impl.componentLogLevels());
  EXPECT_EQ(regular_options_impl->logPath(), test_options_impl.logPath());
  EXPECT_EQ(regular_options_impl->parentShutdownTime(), test_options_impl.parentShutdownTime());
  EXPECT_EQ(regular_options_impl->restartEpoch(), test_options_impl.restartEpoch());
  EXPECT_EQ(regular_options_impl->mode(), test_options_impl.mode());
  EXPECT_EQ(regular_options_impl->fileFlushIntervalMsec(),
            test_options_impl.fileFlushIntervalMsec());
  EXPECT_EQ(regular_options_impl->hotRestartDisabled(), test_options_impl.hotRestartDisabled());
  EXPECT_EQ(regular_options_impl->cpusetThreadsEnabled(), test_options_impl.cpusetThreadsEnabled());
}

TEST_F(OptionsImplTest, SetBothConcurrencyAndCpuset) {
  EXPECT_LOG_CONTAINS(
      "warning",
      "Both --concurrency and --cpuset-threads options are set; not applying --cpuset-threads.",
      std::unique_ptr<OptionsImpl> options =
          createOptionsImpl("envoy -c hello --concurrency 42 --cpuset-threads"));
}

TEST_F(OptionsImplTest, SetCpusetOnly) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy -c hello --cpuset-threads");
  EXPECT_NE(options->concurrency(), 0);
}

#if defined(__linux__)

using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

class OptionsImplPlatformLinuxTest : public testing::Test {
public:
};

TEST_F(OptionsImplPlatformLinuxTest, AffinityTest1) {
  // Success case: cpuset size and hardware thread count are the same.
  unsigned int fake_hw_threads = 4;
  cpu_set_t test_set;
  Api::MockLinuxOsSysCalls linux_os_sys_calls;
  TestThreadsafeSingletonInjector<Api::LinuxOsSysCallsImpl> linux_os_calls(&linux_os_sys_calls);

  // Set cpuset size to be four.
  CPU_ZERO(&test_set);
  for (int i = 0; i < 4; i++) {
    CPU_SET(i, &test_set);
  }

  EXPECT_CALL(linux_os_sys_calls, sched_getaffinity(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(test_set), Return(Api::SysCallIntResult{0, 0})));
  EXPECT_EQ(OptionsImplPlatformLinux::getCpuAffinityCount(fake_hw_threads), 4);
}

TEST_F(OptionsImplPlatformLinuxTest, AffinityTest2) {
  // Success case: cpuset size is half of the hardware thread count.
  unsigned int fake_hw_threads = 16;
  cpu_set_t test_set;
  Api::MockLinuxOsSysCalls linux_os_sys_calls;
  TestThreadsafeSingletonInjector<Api::LinuxOsSysCallsImpl> linux_os_calls(&linux_os_sys_calls);

  // Set cpuset size to be eight.
  CPU_ZERO(&test_set);
  for (int i = 0; i < 8; i++) {
    CPU_SET(i, &test_set);
  }

  EXPECT_CALL(linux_os_sys_calls, sched_getaffinity(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(test_set), Return(Api::SysCallIntResult{0, 0})));
  EXPECT_EQ(OptionsImplPlatformLinux::getCpuAffinityCount(fake_hw_threads), 8);
}

TEST_F(OptionsImplPlatformLinuxTest, AffinityTest3) {
  // Failure case: cpuset size is bigger than the hardware thread count.
  unsigned int fake_hw_threads = 4;
  cpu_set_t test_set;
  Api::MockLinuxOsSysCalls linux_os_sys_calls;
  TestThreadsafeSingletonInjector<Api::LinuxOsSysCallsImpl> linux_os_calls(&linux_os_sys_calls);

  // Set cpuset size to be eight.
  CPU_ZERO(&test_set);
  for (int i = 0; i < 8; i++) {
    CPU_SET(i, &test_set);
  }

  EXPECT_CALL(linux_os_sys_calls, sched_getaffinity(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(test_set), Return(Api::SysCallIntResult{0, 0})));
  EXPECT_EQ(OptionsImplPlatformLinux::getCpuAffinityCount(fake_hw_threads), fake_hw_threads);
}

TEST_F(OptionsImplPlatformLinuxTest, AffinityTest4) {
  // When sched_getaffinity() fails, expect to get the hardware thread count.
  unsigned int fake_hw_threads = 8;
  cpu_set_t test_set;
  Api::MockLinuxOsSysCalls linux_os_sys_calls;
  TestThreadsafeSingletonInjector<Api::LinuxOsSysCallsImpl> linux_os_calls(&linux_os_sys_calls);

  // Set cpuset size to be four.
  CPU_ZERO(&test_set);
  for (int i = 0; i < 4; i++) {
    CPU_SET(i, &test_set);
  }

  EXPECT_CALL(linux_os_sys_calls, sched_getaffinity(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(test_set), Return(Api::SysCallIntResult{-1, 0})));
  EXPECT_EQ(OptionsImplPlatformLinux::getCpuAffinityCount(fake_hw_threads), fake_hw_threads);
}

#endif

} // namespace
} // namespace Envoy
