#include "common/http/codec_wrappers.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

using testing::_;

namespace Envoy {
namespace Http {

class MockStreamEncoderWrapper : public StreamEncoderWrapper {
public:
  MockStreamEncoderWrapper() : StreamEncoderWrapper(inner_encoder_) {}
  void onEncodeComplete() override { encode_complete_ = true; }

  MockStreamEncoder& innerEncoder() { return inner_encoder_; }
  bool encodeComplete() const { return encode_complete_; }

private:
  MockStreamEncoder inner_encoder_;
  bool encode_complete_{};
};

TEST(StreamEncoderWrapper, HeaderOnlyEncode) {
  MockStreamEncoderWrapper wrapper;

  EXPECT_CALL(wrapper.innerEncoder(), encodeHeaders(_, true));
  wrapper.encodeHeaders(TestHeaderMapImpl{{":status", "200"}}, true);
  EXPECT_TRUE(wrapper.encodeComplete());
}

TEST(StreamEncoderWrapper, HeaderAndBodyEncode) {
  MockStreamEncoderWrapper wrapper;

  TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(wrapper.innerEncoder(), encodeHeaders(_, false));
  wrapper.encodeHeaders(response_headers, false);
  EXPECT_FALSE(wrapper.encodeComplete());

  Buffer::OwnedImpl data;
  EXPECT_CALL(wrapper.innerEncoder(), encodeData(_, true));
  wrapper.encodeData(data, true);
  EXPECT_TRUE(wrapper.encodeComplete());
}

TEST(StreamEncoderWrapper, HeaderAndBodyAndTrailersEncode) {
  MockStreamEncoderWrapper wrapper;

  TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(wrapper.innerEncoder(), encodeHeaders(_, false));
  wrapper.encodeHeaders(response_headers, false);
  EXPECT_FALSE(wrapper.encodeComplete());

  Buffer::OwnedImpl data;
  EXPECT_CALL(wrapper.innerEncoder(), encodeData(_, false));
  wrapper.encodeData(data, false);
  EXPECT_FALSE(wrapper.encodeComplete());

  EXPECT_CALL(wrapper.innerEncoder(), encodeTrailers(_));
  wrapper.encodeTrailers(TestHeaderMapImpl{{"trailing", "header"}});
  EXPECT_TRUE(wrapper.encodeComplete());
}

TEST(StreamEncoderWrapper, 100ContinueHeaderEncode) {
  MockStreamEncoderWrapper wrapper;

  EXPECT_CALL(wrapper.innerEncoder(), encode100ContinueHeaders(_));
  wrapper.encode100ContinueHeaders(TestHeaderMapImpl{{":status", "100"}});
  EXPECT_FALSE(wrapper.encodeComplete());

  EXPECT_CALL(wrapper.innerEncoder(), encodeHeaders(_, true));
  wrapper.encodeHeaders(TestHeaderMapImpl{{":status", "200"}}, true);
  EXPECT_TRUE(wrapper.encodeComplete());
}

} // namespace Http
} // namespace Envoy
