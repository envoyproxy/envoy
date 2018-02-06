#include "exe/main_common.h"

#include "gtest/gtest.h"
#include "test/test_common/environment.h"

namespace Envoy {

TEST(MainCommon, ConstructDestruct) {
  std::string config_file = Envoy::TestEnvironment::getCheckedEnvVar("TEST_SRCDIR") +
      "/envoy/configs/google_com_proxy.json";
  const char* argv[] = {"envoy-static", "-c", config_file.c_str(), nullptr};
  MainCommon main_common(3, argv, false);
}

} // Envoy
