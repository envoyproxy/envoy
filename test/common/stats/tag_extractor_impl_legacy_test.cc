#include <string>

#include "envoy/common/exception.h"
#include "envoy/config/metrics/v3/stats.pb.h"

#include "source/common/config/well_known_names.h"
#include "source/common/stats/tag_extractor_impl.h"
#include "source/common/stats/tag_producer_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::testing::ElementsAre;

namespace Envoy {
namespace Stats {

TEST(LegacyTagExtractorTest, LegacyExtAuthzTagExtractors) {
  const auto& tag_names = Config::TagNames::get();

  Tag listener_http_prefix;
  listener_http_prefix.name_ = tag_names.HTTP_CONN_MANAGER_PREFIX;
  listener_http_prefix.value_ = "http_prefix";

  Tag grpc_cluster;
  grpc_cluster.name_ = tag_names.CLUSTER_NAME;
  grpc_cluster.value_ = "grpc_cluster";

  DefaultTagRegexTester regex_tester;

  // ExtAuthz Prefix
  regex_tester.testRegex("http.http_prefix.ext_authz.authpfx.denied",
                         "http.ext_authz.authpfx.denied", {listener_http_prefix});
  regex_tester.testRegex("cluster.grpc_cluster.ext_authz.authpfx.ok",
                         "cluster.ext_authz.authpfx.ok", {grpc_cluster});
}

} // namespace Stats
} // namespace Envoy