#include "envoy/http/filter.h"

#include "common/http/matching/data_impl.h"
#include "common/http/matching/inputs.h"

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
    EXPECT_EQ(input.get(data).data_, "bar");
  }

  TestRequestHeaderMapImpl request_headers({{"header", "baz"}});
  data.onRequestHeaders(request_headers);
  EXPECT_EQ(input.get(data).data_, "baz");
  EXPECT_EQ(input.get(data).data_, "baz");
}
} // namespace Matching
} // namespace Http
} // namespace Envoy
