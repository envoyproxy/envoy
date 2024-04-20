#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/clusters_params.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

struct ParamsCase {
  std::string url_;
  ClustersParams::Format expected_format_;
  Http::Code expected_code_;
};

class ParamsFixture : public testing::TestWithParam<ParamsCase> {};

TEST(ClustersParamsTest, ClustersParamsHasExpectedDefaultValue) {
  ClustersParams params;
  EXPECT_EQ(params.format_, ClustersParams::Format::Text);
}

TEST_P(ParamsFixture, ClustersParamsHasExpectedFormatAndStatusCode) {
  ClustersParams params;
  Buffer::OwnedImpl buffer;
  ParamsCase test_case = GetParam();
  Http::Code code = params.parse(test_case.url_, buffer);

  EXPECT_EQ(params.format_, test_case.expected_format_);
  EXPECT_EQ(code, test_case.expected_code_);
}

INSTANTIATE_TEST_SUITE_P(
    AllCases, ParamsFixture,
    testing::ValuesIn<ParamsCase>({
        {"localhost:1337/clusters", ClustersParams::Format::Text, Http::Code::OK},
        {"localhsot:1337/clusters?format=text", ClustersParams::Format::Text, Http::Code::OK},
        {"localhost:1337/clusters?format=json", ClustersParams::Format::Json, Http::Code::OK},
    }));

} // namespace Server
} // namespace Envoy
