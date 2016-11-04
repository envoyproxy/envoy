#include "server/options_impl.h"

TEST(OptionsImplTest, All) {
  std::vector<const char*> argv;
  argv.push_back("envoy");
  argv.push_back("--concurrency");
  argv.push_back("2");
  argv.push_back("-c");
  argv.push_back("hello");
  argv.push_back("--restart-epoch");
  argv.push_back("1");
  argv.push_back("-l");
  argv.push_back("info");
  argv.push_back("--service-cluster");
  argv.push_back("cluster");
  argv.push_back("--service-node");
  argv.push_back("node");
  argv.push_back("--service-zone");
  argv.push_back("zone");
  argv.push_back("--file-flush-interval-msec");
  argv.push_back("9000");
  argv.push_back("--drain-time-s");
  argv.push_back("60");
  argv.push_back("--parent-shutdown-time-s");
  argv.push_back("90");
  OptionsImpl options(argv.size(), const_cast<char**>(&argv[0]), "1", spdlog::level::warn);
  EXPECT_EQ(2U, options.concurrency());
  EXPECT_EQ("hello", options.configPath());
  EXPECT_EQ(1U, options.restartEpoch());
  EXPECT_EQ(spdlog::level::info, options.logLevel());
  EXPECT_EQ("cluster", options.serviceClusterName());
  EXPECT_EQ("node", options.serviceNodeName());
  EXPECT_EQ("zone", options.serviceZone());
  EXPECT_EQ(std::chrono::milliseconds(9000), options.fileFlushIntervalMsec());
  EXPECT_EQ(std::chrono::seconds(60), options.drainTime());
  EXPECT_EQ(std::chrono::seconds(90), options.parentShutdownTime());
}

TEST(OptionsImplTest, DefaultParams) {
  std::vector<const char*> argv;
  argv.push_back("envoy");
  argv.push_back("-c");
  argv.push_back("hello");
  OptionsImpl options(argv.size(), const_cast<char**>(&argv[0]), "1", spdlog::level::warn);
  EXPECT_EQ(std::chrono::seconds(600), options.drainTime());
  EXPECT_EQ(std::chrono::seconds(900), options.parentShutdownTime());
}
