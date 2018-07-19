#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/exception.h"

#include "common/common/utility.h"
#include "common/stats/stats_impl.h"

#include "server/options_impl.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

using testing::HasSubstr;

namespace Envoy {

namespace {

// Do the ugly work of turning a std::string into a char** and create an OptionsImpl. Args are
// separated by a single space: no fancy quoting or escaping.
std::unique_ptr<OptionsImpl> createOptionsImpl(const std::string& args) {
  std::vector<std::string> words = TestUtility::split(args, ' ');
  std::vector<const char*> argv;
  for (const std::string& s : words) {
    argv.push_back(s.c_str());
  }
  return std::make_unique<OptionsImpl>(
      argv.size(), argv.data(), [](uint64_t, uint64_t, bool) { return "1"; }, spdlog::level::warn);
}

} // namespace

TEST(OptionsImplTest, HotRestartVersion) {
  EXPECT_THROW_WITH_REGEX(createOptionsImpl("envoy --hot-restart-version"), NoServingException,
                          "NoServingException");
}

TEST(OptionsImplTest, InvalidMode) {
  EXPECT_THROW_WITH_REGEX(createOptionsImpl("envoy --mode bogus"), MalformedArgvException, "bogus");
}

TEST(OptionsImplTest, InvalidCommandLine) {
  EXPECT_THROW_WITH_REGEX(createOptionsImpl("envoy --blah"), MalformedArgvException,
                          "Couldn't find match for argument");
}

TEST(OptionsImplTest, v1Allowed) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl(
      "envoy --mode validate --concurrency 2 -c hello --admin-address-path path --restart-epoch 1 "
      "--local-address-ip-version v6 -l info --service-cluster cluster --service-node node "
      "--service-zone zone --file-flush-interval-msec 9000 --drain-time-s 60 --log-format [%v] "
      "--parent-shutdown-time-s 90 --log-path /foo/bar --allow-deprecated-v1-api "
      "--disable-hot-restart");
  EXPECT_EQ(Server::Mode::Validate, options->mode());
  EXPECT_FALSE(options->v2ConfigOnly());
}

TEST(OptionsImplTest, v1Disallowed) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl(
      "envoy --mode validate --concurrency 2 -c hello --admin-address-path path --restart-epoch 1 "
      "--local-address-ip-version v6 -l info --service-cluster cluster --service-node node "
      "--service-zone zone --file-flush-interval-msec 9000 --drain-time-s 60 --log-format [%v] "
      "--parent-shutdown-time-s 90 --log-path /foo/bar --disable-hot-restart");
  EXPECT_EQ(Server::Mode::Validate, options->mode());
  EXPECT_TRUE(options->v2ConfigOnly());
}

TEST(OptionsImplTest, All) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl(
      "envoy --mode validate --concurrency 2 -c hello --admin-address-path path --restart-epoch 1 "
      "--local-address-ip-version v6 -l info --service-cluster cluster --service-node node "
      "--service-zone zone --file-flush-interval-msec 9000 --drain-time-s 60 --log-format [%v] "
      "--parent-shutdown-time-s 90 --log-path /foo/bar --v2-config-only --disable-hot-restart");
  EXPECT_EQ(Server::Mode::Validate, options->mode());
  EXPECT_EQ(2U, options->concurrency());
  EXPECT_EQ("hello", options->configPath());
  EXPECT_TRUE(options->v2ConfigOnly());
  EXPECT_EQ("path", options->adminAddressPath());
  EXPECT_EQ(Network::Address::IpVersion::v6, options->localAddressIpVersion());
  EXPECT_EQ(1U, options->restartEpoch());
  EXPECT_EQ(spdlog::level::info, options->logLevel());
  EXPECT_EQ("[%v]", options->logFormat());
  EXPECT_EQ("/foo/bar", options->logPath());
  EXPECT_EQ("cluster", options->serviceClusterName());
  EXPECT_EQ("node", options->serviceNodeName());
  EXPECT_EQ("zone", options->serviceZone());
  EXPECT_EQ(std::chrono::milliseconds(9000), options->fileFlushIntervalMsec());
  EXPECT_EQ(std::chrono::seconds(60), options->drainTime());
  EXPECT_EQ(std::chrono::seconds(90), options->parentShutdownTime());
  EXPECT_EQ(true, options->hotRestartDisabled());

  options = createOptionsImpl("envoy --mode init_only");
  EXPECT_EQ(Server::Mode::InitOnly, options->mode());
}

TEST(OptionsImplTest, SetAll) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy -c hello");
  bool v2_config_only = options->v2ConfigOnly();
  bool hot_restart_disabled = options->hotRestartDisabled();
  Stats::StatsOptionsImpl stats_options;
  stats_options.max_obj_name_length_ = 54321;
  stats_options.max_stat_suffix_length_ = 1234;

  options->setBaseId(109876);
  options->setConcurrency(42);
  options->setConfigPath("foo");
  options->setConfigYaml("bogus:");
  options->setV2ConfigOnly(!options->v2ConfigOnly());
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
  options->setMaxStats(12345);
  options->setStatsOptions(stats_options);
  options->setHotRestartDisabled(!options->hotRestartDisabled());

  EXPECT_EQ(109876, options->baseId());
  EXPECT_EQ(42U, options->concurrency());
  EXPECT_EQ("foo", options->configPath());
  EXPECT_EQ("bogus:", options->configYaml());
  EXPECT_EQ(!v2_config_only, options->v2ConfigOnly());
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
  EXPECT_EQ(12345U, options->maxStats());
  EXPECT_EQ(stats_options.max_obj_name_length_, options->statsOptions().maxObjNameLength());
  EXPECT_EQ(stats_options.max_stat_suffix_length_, options->statsOptions().maxStatSuffixLength());
  EXPECT_EQ(!hot_restart_disabled, options->hotRestartDisabled());
}

TEST(OptionsImplTest, DefaultParams) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy -c hello");
  EXPECT_EQ(std::chrono::seconds(600), options->drainTime());
  EXPECT_EQ(std::chrono::seconds(900), options->parentShutdownTime());
  EXPECT_EQ("", options->adminAddressPath());
  EXPECT_EQ(Network::Address::IpVersion::v4, options->localAddressIpVersion());
  EXPECT_EQ(Server::Mode::Serve, options->mode());
  EXPECT_EQ(false, options->hotRestartDisabled());
}

TEST(OptionsImplTest, BadCliOption) {
  EXPECT_THROW_WITH_REGEX(createOptionsImpl("envoy -c hello --local-address-ip-version foo"),
                          MalformedArgvException, "error: unknown IP address version 'foo'");
}

TEST(OptionsImplTest, BadObjNameLenOption) {
  EXPECT_THROW_WITH_REGEX(createOptionsImpl("envoy --max-obj-name-len 1"), MalformedArgvException,
                          "'max-obj-name-len' value specified");
}

TEST(OptionsImplTest, BadMaxStatsOption) {
  EXPECT_THROW_WITH_REGEX(createOptionsImpl("envoy --max-stats 1000000000"), MalformedArgvException,
                          "'max-stats' value specified");
}

} // namespace Envoy
