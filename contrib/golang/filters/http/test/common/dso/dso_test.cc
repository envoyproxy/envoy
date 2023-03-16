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
  DsoPtr dso(new DsoInstance(path));
  EXPECT_EQ(dso->envoyGoFilterNewHttpPluginConfig(0, 0), 100);
}

TEST(DsoManagerTest, Pub) {
  auto id = "simple.so";
  auto path = genSoPath(id);

  // get before load
  auto dso = DsoManager::getDsoByID(id);
  EXPECT_EQ(dso, nullptr);

  // first time load
  auto res = DsoManager::load(id, path);
  EXPECT_EQ(res, true);

  // get after load
  dso = DsoManager::getDsoByID(id);
  EXPECT_NE(dso, nullptr);

  // second time load
  res = DsoManager::load(id, path);
  EXPECT_EQ(res, true);
}

} // namespace
} // namespace Dso
} // namespace Envoy
