#include "envoy/http/codes.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/cdn_loop/filter.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CdnLoop {
namespace {

TEST(CdnLoopFilterTest, TestNoHeader) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  CdnLoopFilter filter("cdn", 0);
  filter.setDecoderFilterCallbacks(decoder_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_EQ(filter.decodeHeaders(request_headers, false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(request_headers.get(Http::LowerCaseString("CDN-Loop"))[0]->value().getStringView(),
            "cdn");
}

TEST(CdnLoopFilterTest, OtherCdnsInHeader) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  CdnLoopFilter filter("cdn", 0);
  filter.setDecoderFilterCallbacks(decoder_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{{"CDN-Loop", "cdn1,cdn2"}};

  EXPECT_EQ(filter.decodeHeaders(request_headers, false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(request_headers.get(Http::LowerCaseString("CDN-Loop"))[0]->value().getStringView(),
            "cdn1,cdn2,cdn");
}

TEST(CdnLoopFilterTest, LoopDetected) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  EXPECT_CALL(decoder_callbacks, sendLocalReply(Http::Code::BadGateway, _, _, _, _));
  CdnLoopFilter filter("cdn", 0);
  filter.setDecoderFilterCallbacks(decoder_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{{"CDN-Loop", "cdn"}};

  EXPECT_EQ(filter.decodeHeaders(request_headers, false), Http::FilterHeadersStatus::StopIteration);
}

TEST(CdnLoopFilterTest, MultipleTransitsAllowed) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  EXPECT_CALL(decoder_callbacks, sendLocalReply(Http::Code::BadGateway, _, _, _, _));
  CdnLoopFilter filter("cdn", 3);
  filter.setDecoderFilterCallbacks(decoder_callbacks);

  {
    Http::TestRequestHeaderMapImpl request_headers{};
    EXPECT_EQ(filter.decodeHeaders(request_headers, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(request_headers.get(Http::LowerCaseString("CDN-Loop"))[0]->value().getStringView(),
              "cdn");
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{{"CDN-Loop", "cdn"}};
    EXPECT_EQ(filter.decodeHeaders(request_headers, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(request_headers.get(Http::LowerCaseString("CDN-Loop"))[0]->value().getStringView(),
              "cdn,cdn");
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{{"CDN-Loop", "cdn,cdn"}};
    EXPECT_EQ(filter.decodeHeaders(request_headers, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(request_headers.get(Http::LowerCaseString("CDN-Loop"))[0]->value().getStringView(),
              "cdn,cdn,cdn");
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{{"CDN-Loop", "cdn,cdn,cdn"}};
    EXPECT_EQ(filter.decodeHeaders(request_headers, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(request_headers.get(Http::LowerCaseString("CDN-Loop"))[0]->value().getStringView(),
              "cdn,cdn,cdn,cdn");
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{{"CDN-Loop", "cdn,cdn,cdn,cdn"}};
    EXPECT_EQ(filter.decodeHeaders(request_headers, false),
              Http::FilterHeadersStatus::StopIteration);
  }
}

TEST(CdnLoopFilterTest, MultipleHeadersAllowed) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  CdnLoopFilter filter("cdn", 0);
  filter.setDecoderFilterCallbacks(decoder_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{{"CDN-Loop", "cdn1"}, {"CDN-Loop", "cdn2"}};

  EXPECT_EQ(filter.decodeHeaders(request_headers, false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(request_headers.get(Http::LowerCaseString("CDN-Loop"))[0]->value().getStringView(),
            "cdn1,cdn2,cdn");
}

TEST(CdnLoopFilterTest, UnparseableHeader) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  EXPECT_CALL(decoder_callbacks, sendLocalReply(Http::Code::BadRequest, _, _, _, _));
  CdnLoopFilter filter("cdn", 0);
  filter.setDecoderFilterCallbacks(decoder_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{{"CDN-Loop", ";"}};

  EXPECT_EQ(filter.decodeHeaders(request_headers, false), Http::FilterHeadersStatus::StopIteration);
}

} // namespace
} // namespace CdnLoop
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
