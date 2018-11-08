#include "common/config/cds_json.h"
#include "common/json/json_loader.h"
#include "common/protobuf/utility.h"
#include "common/stats/stats_options_impl.h"

#include "test/common/upstream/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Upstream::makeClusterWithAttribute;
using testing::_;

namespace Envoy {
namespace Config {
namespace {

TEST(CdsJsonTest, TestClusterTranslationMark) {
  const envoy::api::v2::Cluster cluster = makeClusterWithAttribute("\"mark\":5");
  EXPECT_EQ(PROTOBUF_GET_WRAPPED_REQUIRED(cluster, mark), 5);
}

TEST(CdsJsonTest, TestClusterTranslatioMarkMax) {
  const envoy::api::v2::Cluster cluster = makeClusterWithAttribute("\"mark\":4294967295");
  EXPECT_EQ(PROTOBUF_GET_WRAPPED_REQUIRED(cluster, mark), 4294967295);
}

TEST(CdsJsonTest, TestClusterTranslationSrcTransparent) {
  const envoy::api::v2::Cluster cluster = makeClusterWithAttribute("\"src_transparent\":true");
  EXPECT_EQ(PROTOBUF_GET_WRAPPED_REQUIRED(cluster, src_transparent), true);
}

TEST(CdsJsonTest, TestClusterTranslationSrcTransparentFalse) {
  const envoy::api::v2::Cluster cluster = makeClusterWithAttribute("\"src_transparent\":false");
  EXPECT_EQ(PROTOBUF_GET_WRAPPED_REQUIRED(cluster, src_transparent), false);
}
} // namespace
} // namespace Config
} // namespace Envoy
