#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/exception.h"

#include "common/common/utility.h"

#include "server/options_impl.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

using testing::HasSubstr;

namespace Envoy {
// Do the ugly work of turning a std::string into a char** and create an OptionsImpl. Args are
// separated by a single space: no fancy quoting or escaping.
std::unique_ptr<OptionsImpl> createOptionsImpl(const std::string& args) {
  std::vector<std::string> words = TestUtility::split(args, ' ');
  std::vector<const char*> argv;
  for (const std::string& s : words) {
    argv.push_back(s.c_str());
  }
  return std::unique_ptr<OptionsImpl>(new OptionsImpl(argv.size(), const_cast<char**>(&argv[0]),
                                                      [](uint64_t, uint64_t, bool) { return "1"; },
                                                      spdlog::level::warn));
}

TEST(OptionsImplTest, HotRestartVersion) {
  try {
    createOptionsImpl("envoy --hot-restart-version");
    FAIL();
  } catch (const NoServingException& e) {
    SUCCEED();
  }
}

TEST(OptionsImplTest, InvalidMode) {
  try {
    createOptionsImpl("envoy --mode bogus");
    FAIL();
  } catch (const MalformedArgvException& e) {
    EXPECT_THAT(e.what(), HasSubstr("bogus"));
  }
}

TEST(OptionsImplTest, InvalidCommandLine) {
  try {
    createOptionsImpl("envoy --blah");
    FAIL();
  } catch (const MalformedArgvException& e) {
    EXPECT_THAT(e.what(), HasSubstr("Couldn't find match for argument"));
  }
}

TEST(OptionsImplTest, All) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl(
      "envoy --mode validate --concurrency 2 -c hello --admin-address-path path --restart-epoch 1 "
      "--local-address-ip-version v6 -l info --service-cluster cluster --service-node node "
      "--service-zone zone --file-flush-interval-msec 9000 --drain-time-s 60 "
      "--parent-shutdown-time-s 90 --log-path /foo/bar --v2-config-only --disable-hot-restart");
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
  EXPECT_EQ(true, options->hotRestartDisabled());
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
  try {
    createOptionsImpl("envoy -c hello --local-address-ip-version foo");
    FAIL();
  } catch (const MalformedArgvException& e) {
    EXPECT_THAT(e.what(), HasSubstr("error: unknown IP address version 'foo'"));
  }
}

TEST(OptionsImplTest, BadObjNameLenOption) {
  try {
    createOptionsImpl("envoy --max-obj-name-len 1");
    FAIL();
  } catch (const MalformedArgvException& e) {
    EXPECT_THAT(e.what(), HasSubstr("'max-obj-name-len' value specified"));
  }
}
} // namespace Envoy
