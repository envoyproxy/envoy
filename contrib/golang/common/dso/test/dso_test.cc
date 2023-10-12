#include <string>

#include "envoy/registry/registry.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "contrib/golang/common/dso/dso.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Dso {
namespace {

std::string genSoPath(std::string name) {
  return TestEnvironment::substitute("{{ test_rundir }}/contrib/golang/common/dso/test/test_data/" +
                                     name);
}

TEST(DsoInstanceTest, SimpleAPI) {
  auto path = genSoPath("simple.so");
  HttpFilterDsoPtr dso(new HttpFilterDsoImpl(path));
  httpConfig* config = new httpConfig();
  config->config_len = 100;
  EXPECT_EQ(dso->envoyGoFilterNewHttpPluginConfig(config), 100);
}

TEST(DsoManagerTest, Pub) {
  auto id = "simple.so";
  auto plugin_name = "example";
  auto path = genSoPath(id);

  {
    // get before load http filter dso
    auto dso = DsoManager<HttpFilterDsoImpl>::getDsoByPluginName(plugin_name);
    EXPECT_EQ(dso, nullptr);

    // first time load http filter dso
    dso = DsoManager<HttpFilterDsoImpl>::load(id, path, plugin_name);
    EXPECT_NE(dso, nullptr);

    // get after load http filter dso
    dso = DsoManager<HttpFilterDsoImpl>::getDsoByPluginName(plugin_name);
    EXPECT_NE(dso, nullptr);
    httpConfig* config = new httpConfig();
    config->config_len = 200;
    EXPECT_EQ(dso->envoyGoFilterNewHttpPluginConfig(config), 200);

    // second time load http filter dso
    dso = DsoManager<HttpFilterDsoImpl>::load(id, path, plugin_name);
    EXPECT_NE(dso, nullptr);
  }

  {
    // first time load cluster specifier dso
    auto cluster_dso = DsoManager<ClusterSpecifierDsoImpl>::load(id, path);
    EXPECT_NE(cluster_dso, nullptr);

    EXPECT_EQ(cluster_dso->envoyGoClusterSpecifierNewPlugin(0, 0), 200);
  }

  {
    // get before load network filter dso
    auto dso = DsoManager<NetworkFilterDsoImpl>::getDsoByID(id);
    EXPECT_EQ(dso, nullptr);

    // first time load network filter dso
    auto res = DsoManager<NetworkFilterDsoImpl>::load(id, path);
    EXPECT_NE(res, nullptr);

    // get after load network filter dso
    dso = DsoManager<NetworkFilterDsoImpl>::getDsoByID(id);
    EXPECT_NE(dso, nullptr);
    EXPECT_EQ(dso->envoyGoFilterOnNetworkFilterConfig(0, 0, 0, 0), 100);

    // second time load network filter dso
    res = DsoManager<NetworkFilterDsoImpl>::load(id, path);
    EXPECT_NE(dso, nullptr);
  }
}

// missing a symbol
TEST(DsoInstanceTest, BadSo) {
  auto path = genSoPath("bad.so");
  ClusterSpecifierDsoPtr dso(new ClusterSpecifierDsoImpl(path));
  EXPECT_EQ(dso->loaded(), false);
}

// remove plugin config
TEST(DsoInstanceTest, RemovePluginConfig) {
  auto path = genSoPath("simple.so");
  HttpFilterDsoPtr dso(new HttpFilterDsoImpl(path));
  httpConfig* config = new httpConfig();
  config->config_len = 300;
  EXPECT_EQ(dso->envoyGoFilterNewHttpPluginConfig(config), 300);
  // new again, return 0, since it's already existing
  EXPECT_EQ(dso->envoyGoFilterNewHttpPluginConfig(config), 0);

  // remove it
  dso->envoyGoFilterDestroyHttpPluginConfig(300, 0);
  // new again, after removed.
  EXPECT_EQ(dso->envoyGoFilterNewHttpPluginConfig(config), 300);

  // remove twice should be ok
  dso->envoyGoFilterDestroyHttpPluginConfig(300, 0);
  dso->envoyGoFilterDestroyHttpPluginConfig(300, 0);
}

} // namespace
} // namespace Dso
} // namespace Envoy
