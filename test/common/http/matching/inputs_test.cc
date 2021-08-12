#include "envoy/http/filter.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/http/matching/inputs.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Http {
namespace Matching {

TEST(HttpHeadersDataInputBase, ReturnValueNotPersistedBetweenCalls) {
  HttpRequestHeadersDataInput input("header");
  HttpMatchingDataImpl data;

  {
    TestRequestHeaderMapImpl request_headers({{"header", "bar"}});
    data.onRequestHeaders(request_headers);

    EXPECT_EQ(input.get(data).data_, "bar");
  }

  {
    TestRequestHeaderMapImpl request_headers({{"header", "baz"}});
    data.onRequestHeaders(request_headers);
    EXPECT_EQ(input.get(data).data_, "baz");
  }

  TestRequestHeaderMapImpl request_headers({{"not-header", "baz"}});
  data.onRequestHeaders(request_headers);
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_,
            Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
  EXPECT_EQ(result.data_, absl::nullopt);
}
} // namespace Matching
} // namespace Http
} // namespace Envoy
