#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "common/common/utility.h"

#include "server/options_impl.h"

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {
// Do the ugly work of turning a std::string into a char** and create an OptionsImpl. Args are
// separated by a single space: no fancy quoting or escaping.
std::unique_ptr<OptionsImpl> createOptionsImpl(const std::string& args) {
  std::vector<std::string> words = StringUtil::split(args, ' ');
  std::vector<const char*> argv;
  for (const std::string& s : words) {
    argv.push_back(s.c_str());
  }
  return std::unique_ptr<OptionsImpl>(new OptionsImpl(argv.size(), const_cast<char**>(&argv[0]),
                                                      [](uint64_t, uint64_t) { return "1"; },
                                                      spdlog::level::warn));
}

TEST(OptionsImplDeathTest, HotRestartVersion) {
  EXPECT_EXIT(createOptionsImpl("envoy --hot-restart-version"), testing::ExitedWithCode(0), "");
}

TEST(OptionsImplDeathTest, InvalidMode) {
  EXPECT_EXIT(createOptionsImpl("envoy --mode bogus"), testing::ExitedWithCode(1), "bogus");
}

TEST(OptionsImplDeathTest, InvalidCommandLine) {
  EXPECT_EXIT(createOptionsImpl("envoy ---blah"), testing::ExitedWithCode(1), "PARSE ERROR");
}

TEST(OptionsImplTest, All) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl(
      "envoy --mode validate --concurrency 2 -c hello --admin-address-path path --restart-epoch 1 "
      "--local-address-ip-version v6 -l info --service-cluster cluster --service-node node "
      "--service-zone zone --file-flush-interval-msec 9000 --drain-time-s 60 "
      "--parent-shutdown-time-s 90 --log-path /foo/bar --v2-config-only");
  EXPECT_EQ(Server::Mode::Validate, options->mode());
  EXPECT_EQ(2U, options->concurrency());
  EXPECT_EQ("hello", options->configPath());
  EXPECT_TRUE(options->v2ConfigOnly());
  EXPECT_EQ("path", options->adminAddressPath());
  EXPECT_EQ(Network::Address::IpVersion::v6, options->localAddressIpVersion());
  EXPECT_EQ(1U, options->restartEpoch());
  EXPECT_EQ(spdlog::level::info, options->logLevel());
  EXPECT_EQ("/foo/bar", options->logPath());
  EXPECT_EQ("cluster", options->serviceClusterName());
  EXPECT_EQ("node", options->serviceNodeName());
  EXPECT_EQ("zone", options->serviceZone());
  EXPECT_EQ(std::chrono::milliseconds(9000), options->fileFlushIntervalMsec());
  EXPECT_EQ(std::chrono::seconds(60), options->drainTime());
  EXPECT_EQ(std::chrono::seconds(90), options->parentShutdownTime());
}

TEST(OptionsImplTest, DefaultParams) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy -c hello");
  EXPECT_EQ(std::chrono::seconds(600), options->drainTime());
  EXPECT_EQ(std::chrono::seconds(900), options->parentShutdownTime());
  EXPECT_EQ("", options->adminAddressPath());
  EXPECT_EQ(Network::Address::IpVersion::v4, options->localAddressIpVersion());
  EXPECT_EQ(Server::Mode::Serve, options->mode());
}

TEST(OptionsImplTest, BadCliOption) {
  EXPECT_DEATH(createOptionsImpl("envoy -c hello --local-address-ip-version foo"),
               "error: unknown IP address version 'foo'");
}

TEST(OptionsImplTest, BadObjNameLenOption) {
  EXPECT_DEATH(createOptionsImpl("envoy --max-obj-name-len 1"),
               "error: the 'max-obj-name-len' value specified");
}
} // namespace Envoy
