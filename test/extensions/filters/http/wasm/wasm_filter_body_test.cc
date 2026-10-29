#include "test/extensions/filters/http/wasm/wasm_filter_test_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

INSTANTIATE_TEST_SUITE_P(RuntimesAndLanguages, WasmHttpFilterTest,
                         Envoy::Extensions::Common::Wasm::host_test_runtime_and_language_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);

TEST_P(WasmHttpFilterTest, BodyRequestReadBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}, {"x-test-operation", "ReadBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  filter().onDestroy();
}

// Script that prepends and appends to the body.

TEST_P(WasmHttpFilterTest, BodyRequestPrependAndAppendToBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(),
              log_(spdlog::level::err, Eq(absl::string_view("onBody prepend.hello.append"))));
  EXPECT_CALL(filter(), log_(spdlog::level::err,
                             Eq(absl::string_view("onBody prepend.prepend.hello.append.append"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "PrependAndAppendToBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  if (std::get<1>(GetParam()) == "rust") {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data, true));
  } else {
    // This status is not available in the rust SDK.
    // TODO: update all SDKs to the new revision of the spec and update the tests accordingly.
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter().decodeData(data, true));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter().encodeData(data, true));
  }
  filter().onDestroy();
}

// Script that replaces the body.

TEST_P(WasmHttpFilterTest, BodyRequestReplaceBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody replace")))).Times(2);
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "ReplaceBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  if (std::get<1>(GetParam()) == "rust") {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data, true));
  } else {
    // This status is not available in the rust SDK.
    // TODO: update all SDKs to the new revision of the spec and update the tests accordingly.
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter().decodeData(data, true));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter().encodeData(data, true));
  }
  filter().onDestroy();
}

// Script that replaces the first character in the body.

TEST_P(WasmHttpFilterTest, BodyRequestPartialReplaceBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(),
              log_(spdlog::level::err, Eq(absl::string_view("onBody partial.replace.ello"))));
  EXPECT_CALL(filter(), log_(spdlog::level::err,
                             Eq(absl::string_view("onBody partial.replace.artial.replace.ello"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "PartialReplaceBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data, true));
  filter().onDestroy();
}

// Script that removes the body.

TEST_P(WasmHttpFilterTest, BodyRequestRemoveBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody "))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "RemoveBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  filter().onDestroy();
}

// Script that removes the first character from the body.

TEST_P(WasmHttpFilterTest, BodyRequestPartialRemoveBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody ello"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "PartialRemoveBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  filter().onDestroy();
}

// Script that buffers the body.

TEST_P(WasmHttpFilterTest, BodyRequestBufferBody) {
  setupTest("body");
  setupFilter();

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "BufferBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));

  Buffer::OwnedImpl bufferedBody;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&bufferedBody));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
      .WillRepeatedly([&bufferedBody](BufferFunction f) { f(bufferedBody); });

  Buffer::OwnedImpl data1("hello");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello"))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().decodeData(data1, false));
  // The first call to decodeData() will not result in the addDecodedData() callback being called
  // because the filter doesn't yet know if the VM's onRequestBody() will return
  // StopIterationAndBuffer or Continue. Add the data to the buffer manually for the test.
  bufferedBody.add(data1);

  // The second call to decodeData() will result in the addDecodedData() callback being called
  // because the VM's onRequestBody() returned StopIterationAndBuffer.
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, _))
      .WillOnce([&bufferedBody](Buffer::Instance& data, bool) { bufferedBody.move(data); });

  Buffer::OwnedImpl data2(" again ");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello again "))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().decodeData(data2, false));

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, _))
      .WillOnce([&bufferedBody](Buffer::Instance& data, bool) { bufferedBody.move(data); });

  Buffer::OwnedImpl data3("hello");
  EXPECT_CALL(filter(),
              log_(spdlog::level::err, Eq(absl::string_view("onBody hello again hello"))));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data3, true));

  // Verify that the response still works even though we buffered the request.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"x-test-operation", "ReadBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().encodeHeaders(response_headers, false));
  // Should not buffer this time
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello")))).Times(2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data1, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data1, true));

  filter().onDestroy();
}

TEST_P(WasmHttpFilterTest, BodyRequestBufferBodyAndDestroyFilterInTheAddDecodedDataCallback) {
  setupTest("body");
  setupFilter();

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "BufferBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));

  Buffer::OwnedImpl bufferedBody;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&bufferedBody));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
      .WillRepeatedly([&bufferedBody](BufferFunction f) { f(bufferedBody); });

  Buffer::OwnedImpl data1("hello");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello"))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().decodeData(data1, false));
  // The first call to decodeData() will not result in the addDecodedData() callback being called
  // because the filter doesn't yet know if the VM's onRequestBody() will return
  // StopIterationAndBuffer or Continue. Add the data to the buffer manually for the test.
  bufferedBody.add(data1);

  // The second call to decodeData() will result in the addDecodedData() callback being called
  // because the VM's onRequestBody() returned StopIterationAndBuffer.
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, _))
      .WillOnce([&bufferedBody](Buffer::Instance& data, bool) { bufferedBody.move(data); });

  Buffer::OwnedImpl data2(" again ");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello again "))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().decodeData(data2, false));

  // The addDecodedData() may result in buffer overflow and the local reply will be sent. It may
  // destroy the filter. We mock the onDestroy() to ensure the filter will not call into the VM
  // after it is destroyed.
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, _)).WillOnce([this](Buffer::Instance&, bool) {
    filter().onDestroy();
  });

  Buffer::OwnedImpl data3("hello");
  EXPECT_CALL(filter(), log_(_, _)).Times(0);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().decodeData(data3, true));
}

// Script that prepends and appends to the buffered body.

TEST_P(WasmHttpFilterTest, BodyRequestPrependAndAppendToBufferedBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(),
              log_(spdlog::level::err, Eq(absl::string_view("onBody prepend.hello.append"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"}, {"x-test-operation", "PrependAndAppendToBufferedBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  filter().onDestroy();
}

// Script that replaces the buffered body.

TEST_P(WasmHttpFilterTest, BodyRequestReplaceBufferedBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody replace"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "ReplaceBufferedBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  filter().onDestroy();
}

// Script that replaces the first character in the buffered body.

TEST_P(WasmHttpFilterTest, BodyRequestPartialReplaceBufferedBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(),
              log_(spdlog::level::err, Eq(absl::string_view("onBody partial.replace.ello"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"}, {"x-test-operation", "PartialReplaceBufferedBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  filter().onDestroy();
}

// Script that removes the buffered body.

TEST_P(WasmHttpFilterTest, BodyRequestRemoveBufferedBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody "))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "RemoveBufferedBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  filter().onDestroy();
}

// Script that removes the first character from the buffered body.

TEST_P(WasmHttpFilterTest, BodyRequestPartialRemoveBufferedBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody ello"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "PartialRemoveBufferedBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  filter().onDestroy();
}

// Script that buffers the first part of the body and streams the rest

TEST_P(WasmHttpFilterTest, BodyRequestBufferThenStreamBody) {
  setupTest("body");
  setupFilter();

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));

  Buffer::OwnedImpl bufferedBody;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&bufferedBody));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
      .WillRepeatedly([&bufferedBody](BufferFunction f) { f(bufferedBody); });

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"x-test-operation", "BufferTwoBodies"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().encodeHeaders(response_headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello"))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().decodeData(data1, false));
  bufferedBody.add(data1);

  Buffer::OwnedImpl data2(", there, ");
  bufferedBody.add(data2);
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello, there, "))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().decodeData(data2, false));

  // Previous callbacks returned "Buffer" so we have buffered so far
  Buffer::OwnedImpl data3("world!");
  bufferedBody.add(data3);
  EXPECT_CALL(filter(),
              log_(spdlog::level::err, Eq(absl::string_view("onBody hello, there, world!"))));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data3, false));

  // Last callback returned "continue" so we just see individual chunks.
  Buffer::OwnedImpl data4("So it's ");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody So it's "))));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data4, false));

  Buffer::OwnedImpl data5("goodbye, then!");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody goodbye, then!"))));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data5, true));

  filter().onDestroy();
}

// Script that buffers the first part of the body and streams the rest

TEST_P(WasmHttpFilterTest, BodyResponseBufferThenStreamBody) {
  setupTest("body");
  setupFilter();

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));

  Buffer::OwnedImpl bufferedBody;
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_))
      .WillRepeatedly([&bufferedBody](BufferFunction f) { f(bufferedBody); });

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"x-test-operation", "BufferTwoBodies"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().encodeHeaders(response_headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello"))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().encodeData(data1, false));
  // The first call to encodeData() will not result in the addEncodedData() callback being called
  // because the filter doesn't yet know if the VM's onResponseBody() will return
  // StopIterationAndBuffer or Continue. Add the data to the buffer manually for the test.
  bufferedBody.add(data1);

  // The second call to encodeData() will result in the addEncodedData() callback being called
  // because the VM's onResponseBody() returned StopIterationAndBuffer.
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, _))
      .WillOnce([&bufferedBody](Buffer::Instance& data, bool) { bufferedBody.move(data); });

  Buffer::OwnedImpl data2(", there, ");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello, there, "))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().encodeData(data2, false));

  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, _))
      .WillOnce([&bufferedBody](Buffer::Instance& data, bool) { bufferedBody.move(data); });

  Buffer::OwnedImpl data3("world!");
  EXPECT_CALL(filter(),
              log_(spdlog::level::err, Eq(absl::string_view("onBody hello, there, world!"))));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data3, false));

  // Last callback returned "continue" so we just see individual chunks.
  Buffer::OwnedImpl data4("So it's ");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody So it's "))));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data4, false));

  Buffer::OwnedImpl data5("goodbye, then!");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody goodbye, then!"))));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data5, true));

  filter().onDestroy();
}

// Script testing AccessLog::Instance::log.

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
