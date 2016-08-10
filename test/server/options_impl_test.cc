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
  argv.push_back("3");
  argv.push_back("--service-cluster");
  argv.push_back("cluster");
  argv.push_back("--service-node");
  argv.push_back("node");
  argv.push_back("--service-zone");
  argv.push_back("zone");
  OptionsImpl options(argv.size(), const_cast<char**>(&argv[0]), "1", spdlog::level::notice);
  EXPECT_EQ(2U, options.concurrency());
  EXPECT_EQ("hello", options.configPath());
  EXPECT_EQ(1U, options.restartEpoch());
  EXPECT_EQ(3U, options.logLevel());
  EXPECT_EQ("cluster", options.serviceClusterName());
  EXPECT_EQ("node", options.serviceNodeName());
  EXPECT_EQ("zone", options.serviceZone());
}
