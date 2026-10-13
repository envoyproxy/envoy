#pragma once
#include "envoy/stats/scope.h"

#include "test/test_common/environment.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

/**
 * Reads the value of a ``dynamic_modules.<leaf>`` config-load-failure counter tagged with the given
 * ``config_name`` (the counters emitted by ``incrementLoadFailure`` in the dynamic_modules source).
 * Re-creates the same tagged counter via an idempotent lookup, so it does not depend on how the
 * store renders tags into a flat name. Returns 0 if the counter has never been incremented.
 *
 * @param scope the stats scope the counter lives on (the server scope for load-failure counters).
 * @param leaf the leaf stat name, e.g. ``module_load_error`` or ``config_init_error``.
 * @param config_name the ``config_name`` tag value (the configured extension instance name).
 */
uint64_t failureCounter(Stats::Scope& scope, absl::string_view leaf, absl::string_view config_name);

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

/**
 * Helper class to set up the dynamic modules test environment.
 */
class DynamicModulesTestEnvironment {
public:
  /**
   * Sets the ENVOY_DYNAMIC_MODULES_SEARCH_PATH environment variable to the test data directory.
   */
  static void setModulesSearchPath();
};

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
