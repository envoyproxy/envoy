#include "source/common/http/codec_wrappers.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

using testing::_;

namespace Envoy {
namespace Http {

class MockRequestEncoderWrapper : public RequestEncoderWrapper {
public:
  MockRequestEncoderWrapper() : RequestEncoderWrapper(&inner_encoder_) {}
  void onEncodeComplete() override { encode_complete_ = true; }

  MockRequestEncoder& innerEncoder() { return inner_encoder_; }
  bool encodeComplete() const { return encode_complete_; }

private:
  MockRequestEncoder inner_encoder_;
  bool encode_complete_{};
};

TEST(RequestEncoderWrapper, HeaderOnlyEncode) {
  MockRequestEncoderWrapper wrapper;

  EXPECT_CALL(wrapper.innerEncoder(), encodeHeaders(_, true));
  EXPECT_TRUE(
      wrapper
          .encodeHeaders(
              TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}, {":authority", "foo"}},
              true)
          .ok());
  EXPECT_TRUE(wrapper.encodeComplete());
}

TEST(RequestEncoderWrapper, HeaderAndBodyEncode) {
  MockRequestEncoderWrapper wrapper;

  EXPECT_CALL(wrapper.innerEncoder(), encodeHeaders(_, false));
  EXPECT_TRUE(
      wrapper
          .encodeHeaders(
              TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}, {":authority", "foo"}},
              false)
          .ok());
  EXPECT_FALSE(wrapper.encodeComplete());

  Buffer::OwnedImpl data;
  EXPECT_CALL(wrapper.innerEncoder(), encodeData(_, true));
  wrapper.encodeData(data, true);
  EXPECT_TRUE(wrapper.encodeComplete());
}

TEST(RequestEncoderWrapper, HeaderAndBodyAndTrailersEncode) {
  MockRequestEncoderWrapper wrapper;

  EXPECT_CALL(wrapper.innerEncoder(), encodeHeaders(_, false));
  EXPECT_TRUE(
      wrapper
          .encodeHeaders(
              TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}, {":authority", "foo"}},
              false)
          .ok());
  EXPECT_FALSE(wrapper.encodeComplete());

  Buffer::OwnedImpl data;
  EXPECT_CALL(wrapper.innerEncoder(), encodeData(_, false));
  wrapper.encodeData(data, false);
  EXPECT_FALSE(wrapper.encodeComplete());

  EXPECT_CALL(wrapper.innerEncoder(), encodeTrailers(_));
  wrapper.encodeTrailers(TestRequestTrailerMapImpl{{"trailing", "header"}});
  EXPECT_TRUE(wrapper.encodeComplete());
}

} // namespace Http
} // namespace Envoy
