#include <string>

#include "envoy/registry/registry.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "contrib/golang/filters/http/source/common/dso/dso.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Dso {
namespace {

std::string genSoPath(std::string name) {
  return TestEnvironment::substitute(
      "{{ test_rundir }}/contrib/golang/filters/http/test/common/dso/test_data/" + name);
}

TEST(DsoInstanceTest, SimpleAPI) {
  auto path = genSoPath("simple.so");
  DsoInstance* dso = new DsoInstance(path);
  EXPECT_EQ(dso->envoyGoFilterNewHttpPluginConfig(0, 0), 100);
  delete dso;
}

TEST(DsoInstanceManagerTest, Pub) {
  auto id = "simple.so";
  auto path = genSoPath(id);

  // get before pub
  auto dso = DsoInstanceManager::getDsoInstanceByID(id);
  EXPECT_EQ(dso, nullptr);

  // first time pub
  auto res = DsoInstanceManager::pub(id, path);
  EXPECT_EQ(res, true);

  // get after pub
  dso = DsoInstanceManager::getDsoInstanceByID(id);
  EXPECT_NE(dso, nullptr);

  // second time pub
  res = DsoInstanceManager::pub(id, path);
  EXPECT_EQ(res, true);
}

} // namespace
} // namespace Dso
} // namespace Envoy
