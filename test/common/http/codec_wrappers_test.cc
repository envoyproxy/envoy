#include "source/common/http/codec_wrappers.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

using testing::_;

namespace Envoy {
namespace Http {

class MockResponseDecoderWrapper : public ResponseDecoderWrapper {
public:
  explicit MockResponseDecoderWrapper(MockResponseDecoder& inner_decoder)
      : ResponseDecoderWrapper(inner_decoder) {}
  void onDecodeComplete() override {}
  void onPreDecodeComplete() override {}
};

TEST(MockResponseDecoderWrapper, dumpState) {
  MockResponseDecoder inner_decoder;
  MockResponseDecoderWrapper wrapper(inner_decoder);

  std::stringstream os;
  EXPECT_CALL(inner_decoder, dumpState(_, _));
  wrapper.dumpState(os, 0);
}

TEST(MockResponseDecoderWrapper, decoderDestroyedBeforeDecoding) {
  TestScopedRuntime runtime;
  runtime.mergeValues({{"envoy.reloadable_features.abort_when_accessing_dead_decoder", "false"}});
  auto inner_decoder = std::make_unique<MockResponseDecoder>();
  MockResponseDecoderWrapper wrapper(*inner_decoder);

  inner_decoder.reset();

  EXPECT_ENVOY_BUG(
      wrapper.decodeHeaders(ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}},
                            true),
      "Wrapped decoder use after free detected");

  EXPECT_ENVOY_BUG(wrapper.decode1xxHeaders(
                       ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "100"}}}),
                   "Wrapped decoder use after free detected");

  Buffer::OwnedImpl data("foo");
  EXPECT_ENVOY_BUG(wrapper.decodeData(data, true), "Wrapped decoder use after free detected");

  EXPECT_ENVOY_BUG(wrapper.decodeTrailers(
                       ResponseTrailerMapPtr{new TestResponseTrailerMapImpl{{"key", "value"}}}),
                   "Wrapped decoder use after free detected");

  MetadataMapPtr metadata = std::make_unique<MetadataMap>();
  (*metadata)["key1"] = "value1";
  EXPECT_ENVOY_BUG(wrapper.decodeMetadata(std::move(metadata)),
                   "Wrapped decoder use after free detected");

  std::stringstream os;
  EXPECT_ENVOY_BUG(wrapper.dumpState(os, 0), "Wrapped decoder use after free detected");
}

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

  EXPECT_CALL(wrapper.innerEncoder(), http1StreamEncoderOptions());
  wrapper.http1StreamEncoderOptions();

  EXPECT_CALL(wrapper.innerEncoder(), encodeTrailers(_));
  wrapper.encodeTrailers(TestRequestTrailerMapImpl{{"trailing", "header"}});
  EXPECT_TRUE(wrapper.encodeComplete());
}

} // namespace Http
} // namespace Envoy
