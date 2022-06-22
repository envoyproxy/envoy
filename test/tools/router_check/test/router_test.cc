#include <string>

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"
#include "test/tools/router_check/router.h"
#include "test/tools/router_check/validation.pb.h"

#include "absl/strings/str_join.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

constexpr char kDir[] = "test/tools/router_check/test/config/";

TEST(RouterCheckTest, RouterCheckTestRoutesSuccessTest) {
  ScopedInjectableLoader<Regex::Engine> engine(std::make_unique<Regex::GoogleReEngine>());
  const std::string config_filename_ =
      TestEnvironment::runfilesPath(absl::StrCat(kDir, "TestRoutes.yaml"));
  const std::string tests_filename_ =
      TestEnvironment::runfilesPath(absl::StrCat(kDir, "TestRoutes.golden.proto.json"));
  RouterCheckTool checktool = RouterCheckTool::create(config_filename_, false);
  const std::vector<envoy::RouterCheckToolSchema::ValidationItemResult> test_results =
      checktool.compareEntries(tests_filename_);
  EXPECT_EQ(test_results.size(), 33);
  for (const auto& test_result : test_results) {
    EXPECT_TRUE(test_result.test_passed());
    EXPECT_FALSE(test_result.has_failure());
  }
}

TEST(RouterCheckTest, RouterCheckTestRoutesSuccessShowOnlyFailuresTest) {
  ScopedInjectableLoader<Regex::Engine> engine(std::make_unique<Regex::GoogleReEngine>());
  const std::string config_filename_ =
      TestEnvironment::runfilesPath(absl::StrCat(kDir, "TestRoutes.yaml"));
  const std::string tests_filename_ =
      TestEnvironment::runfilesPath(absl::StrCat(kDir, "TestRoutes.golden.proto.json"));
  RouterCheckTool checktool = RouterCheckTool::create(config_filename_, false);
  checktool.setOnlyShowFailures();
  const std::vector<envoy::RouterCheckToolSchema::ValidationItemResult> test_results =
      checktool.compareEntries(tests_filename_);
  EXPECT_EQ(test_results.size(), 0);
}

TEST(RouterCheckTest, RouterCheckTestRoutesFailuresTest) {
  ScopedInjectableLoader<Regex::Engine> engine(std::make_unique<Regex::GoogleReEngine>());
  const std::string config_filename_ =
      TestEnvironment::runfilesPath(absl::StrCat(kDir, "TestRoutes.yaml"));
  const std::string tests_filename_ =
      TestEnvironment::runfilesPath(absl::StrCat(kDir, "TestRoutesFailures.golden.proto.json"));
  RouterCheckTool checktool = RouterCheckTool::create(config_filename_, false);
  checktool.setOnlyShowFailures();
  const std::vector<envoy::RouterCheckToolSchema::ValidationItemResult> test_results =
      checktool.compareEntries(tests_filename_);
  EXPECT_EQ(test_results.size(), 1);
  envoy::RouterCheckToolSchema::ValidationItemResult test_result = test_results[0];
  std::string expected_result_str = R"pb(
      test_name: "ResponseHeaderMatches Failures"
      failure {
        response_header_match_failures {
          header_matcher {
            name: "content-type"
            string_match {
              exact: "text/plain"
            }
            invert_match: true
          }
          actual_header_value {
            value: "text/plain"
          }
        }
        response_header_match_failures {
          header_matcher {
            name: "content-length",
            range_match {
              start: 100,
              end: 1000
            }
          }
          actual_header_value {
            value: "25"
          }
        }
        response_header_match_failures {
          header_matcher {
            name: "x-ping-response",
            string_match: {
              exact: "pong"
            }
          }
          actual_header_value {
            value: "yes"
          }
        }
        response_header_match_failures {
          header_matcher {
            name: "x-ping-response",
            present_match: true,
            invert_match: true
          }
          actual_header_value {
            value: "yes"
          }
        }
        response_header_match_failures {
          header_matcher {
            name: "x-pong-response",
            present_match: true,
            invert_match: false
          }
          actual_header_value {
            value: ""
          }
        }
      }
  )pb";
  envoy::RouterCheckToolSchema::ValidationItemResult expected_result_proto;
  Protobuf::TextFormat::ParseFromString(expected_result_str, &expected_result_proto);

  EXPECT_TRUE(TestUtility::protoEqual(expected_result_proto, test_result, true));
}

} // namespace
} // namespace Envoy
