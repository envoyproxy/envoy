#pragma once
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

/**
 * Class to test the identical behavior of the dynamic module among different languages.
 */
class DynamicModuleTestLanguages : public ::testing::TestWithParam<std::string> {
public:
  static std::string languageParamToTestName(const ::testing::TestParamInfo<std::string>& info) {
    return info.param;
  };
};

/**
 * This loads a shared object file from the test_data directory.
 * @param name the name of the test case.
 * @param language the language of the shared object file.
 */
std::string testSharedObjectPath(std::string name, std::string language);

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
