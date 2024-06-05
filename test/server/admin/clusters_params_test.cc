#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/clusters_params.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

TEST(ClustersParamsTest, FormatDefaultsToTextWhenNotSupplied) {
  ClustersParams params;
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(params.format_, ClustersParams::Format::Text);
  EXPECT_EQ(params.parse("localhost:1337/clusters", buffer), Http::Code::OK);
}

TEST(ClustersParamsTest, FormatSetToTextWhenTextSupplied) {
  ClustersParams params;
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(params.format_, ClustersParams::Format::Text);
  EXPECT_EQ(params.parse("localhost:1337/clusters?format=text", buffer), Http::Code::OK);
}

TEST(ClustersParamsTest, FormatSetToJsonWhenJsonSupplied) {
  ClustersParams params;
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(params.format_, ClustersParams::Format::Text);
  EXPECT_EQ(params.parse("localhost:1337/clusters?format=json", buffer), Http::Code::OK);
}

TEST(ClustersParamsTest, ReturnsBadRequestWhenInvalidFormatSupplied) {
  ClustersParams params;
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(params.parse("localhost:1337/clusters?format=fail", buffer), Http::Code::BadRequest);
}

} // namespace
} // namespace Server
} // namespace Envoy
