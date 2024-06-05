#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/clusters_params.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

TEST(ClustersParamsTest, FormatDefaultsToTextWhenNotSupplied) {
  ClustersParams params;
  Buffer::OwnedImpl buffer;
  std::string url = "localhost:1337/clusters";
  ClustersParams::Format expected_format = ClustersParams::Format::Text;
  Http::Code expected_code = Http::Code::OK;

  Http::Code code = params.parse(url, buffer);

  EXPECT_EQ(params.format_, expected_format);
  EXPECT_EQ(code, expected_code);
}

TEST(ClustersParamsTest, FormatSetToTextWhenTextSupplied) {
  ClustersParams params;
  Buffer::OwnedImpl buffer;
  std::string url = "localhost:1337/clusters?format=text";
  ClustersParams::Format expected_format = ClustersParams::Format::Text;
  Http::Code expected_code = Http::Code::OK;

  Http::Code code = params.parse(url, buffer);

  EXPECT_EQ(params.format_, expected_format);
  EXPECT_EQ(code, expected_code);
}

TEST(ClustersParamsTest, FormatSetToJsonWhenJsonSupplied) {
  ClustersParams params;
  Buffer::OwnedImpl buffer;
  std::string url = "localhost:1337/clusters?format=json";
  ClustersParams::Format expected_format = ClustersParams::Format::Json;
  Http::Code expected_code = Http::Code::OK;

  Http::Code code = params.parse(url, buffer);

  EXPECT_EQ(params.format_, expected_format);
  EXPECT_EQ(code, expected_code);
}

TEST(ClustersParamsTest, ReturnsBadRequestWhenInvalidFormatSupplied) {
  ClustersParams params;
  Buffer::OwnedImpl buffer;
  std::string url = "localhost:1337/clusters?format=fail";
  Http::Code expected_code = Http::Code::BadRequest;

  Http::Code code = params.parse(url, buffer);

  EXPECT_EQ(code, expected_code);
}

} // namespace
} // namespace Server
} // namespace Envoy
