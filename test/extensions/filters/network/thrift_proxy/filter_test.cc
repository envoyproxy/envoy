#include "common/buffer/buffer_impl.h"
#include "common/stats/stats_impl.h"

#include "extensions/filters/network/thrift_proxy/buffer_helper.h"
#include "extensions/filters/network/thrift_proxy/filter.h"

#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class ThriftFilterTest : public testing::Test {
public:
  ThriftFilterTest() {}

  void initializeFilter() {
    for (auto counter : store_.counters()) {
      counter->reset();
    }

    filter_.reset(new Filter("test.", store_));
    filter_->initializeReadFilterCallbacks(read_filter_callbacks_);
    filter_->onNewConnection();

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  void writeFramedBinaryMessage(Buffer::Instance& buffer, MessageType msg_type, int32_t seq_id) {
    uint8_t mt = static_cast<int8_t>(msg_type);
    uint8_t s1 = (seq_id >> 24) & 0xFF;
    uint8_t s2 = (seq_id >> 16) & 0xFF;
    uint8_t s3 = (seq_id >> 8) & 0xFF;
    uint8_t s4 = seq_id & 0xFF;

    addSeq(buffer, {
                       0x00, 0x00, 0x00, 0x1d,                          // framed: 29 bytes
                       0x80, 0x01, 0x00, mt,                            // binary proto, type
                       0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e',      // message name
                       s1,   s2,   s3,   s4,                            // sequence id
                       0x0b, 0x00, 0x00,                                // begin string field
                       0x00, 0x00, 0x00, 0x05, 'f', 'i', 'e', 'l', 'd', // string
                       0x00,                                            // stop field
                   });
  }

  void writePartialFramedBinaryMessage(Buffer::Instance& buffer, MessageType msg_type,
                                       int32_t seq_id, bool start) {
    if (start) {
      uint8_t mt = static_cast<int8_t>(msg_type);
      uint8_t s1 = (seq_id >> 24) & 0xFF;
      uint8_t s2 = (seq_id >> 16) & 0xFF;
      uint8_t s3 = (seq_id >> 8) & 0xFF;
      uint8_t s4 = seq_id & 0xFF;

      addSeq(buffer, {
                         0x00, 0x00, 0x00, 0x2d,                     // framed: 45 bytes
                         0x80, 0x01, 0x00, mt,                       // binary proto, type
                         0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                         s1,   s2,   s3,   s4,                       // sequence id
                         0x0c, 0x00, 0x00,                           // begin struct field
                         0x0b, 0x00, 0x01,                           // begin string field
                         0x00, 0x00, 0x00, 0x05                      // string length only
                     });
    } else {
      addSeq(buffer, {
                         'f',  'i',  'e',  'l',  'd',                     // string data
                         0x0b, 0x00, 0x02,                                // begin string field
                         0x00, 0x00, 0x00, 0x05, 'x', 'x', 'x', 'x', 'x', // string
                         0x00,                                            // stop field
                         0x00,                                            // stop field
                     });
    }
  }

  void writeFramedBinaryTApplicationException(Buffer::Instance& buffer, int32_t seq_id) {
    uint8_t s1 = (seq_id >> 24) & 0xFF;
    uint8_t s2 = (seq_id >> 16) & 0xFF;
    uint8_t s3 = (seq_id >> 8) & 0xFF;
    uint8_t s4 = seq_id & 0xFF;

    addSeq(buffer, {
                       0x00, 0x00, 0x00, 0x24,                          // framed: 36 bytes
                       0x80, 0x01, 0x00, 0x03,                          // binary, exception
                       0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e',      // message name
                       s1,   s2,   s3,   s4,                            // sequence id
                       0x0B, 0x00, 0x01,                                // begin string field
                       0x00, 0x00, 0x00, 0x05, 'e', 'r', 'r', 'o', 'r', // string
                       0x08, 0x00, 0x02,                                // begin i32 field
                       0x00, 0x00, 0x00, 0x01,                          // exception type 1
                       0x00,                                            // stop field
                   });
  }

  void writeFramedBinaryIDLException(Buffer::Instance& buffer, int32_t seq_id) {
    uint8_t s1 = (seq_id >> 24) & 0xFF;
    uint8_t s2 = (seq_id >> 16) & 0xFF;
    uint8_t s3 = (seq_id >> 8) & 0xFF;
    uint8_t s4 = seq_id & 0xFF;

    addSeq(buffer, {
                       0x00, 0x00, 0x00, 0x23,                     // framed: 35 bytes
                       0x80, 0x01, 0x00, 0x02,                     // binary proto, reply
                       0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                       s1,   s2,   s3,   s4,                       // sequence id
                       0x0C, 0x00, 0x02,                           // begin exception struct
                       0x0B, 0x00, 0x01,                           // begin string field
                       0x00, 0x00, 0x00, 0x03, 'e', 'r', 'r',      // string
                       0x00,                                       // exception struct stop
                       0x00,                                       // reply struct stop field
                   });
  }

  Buffer::OwnedImpl buffer_;
  Buffer::OwnedImpl write_buffer_;
  Stats::IsolatedStoreImpl store_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
};

TEST_F(ThriftFilterTest, OnDataHandlesThriftCall) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(ThriftFilterTest, OnDataHandlesThriftOneWay) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Oneway, 0x0F);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_call").value());
  EXPECT_EQ(1U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(ThriftFilterTest, OnDataHandlesFrameSplitAcrossBuffers) {
  initializeFilter();

  writePartialFramedBinaryMessage(buffer_, MessageType::Call, 0x10, true);
  std::string expected_contents = bufferToString(buffer_);
  uint64_t len = buffer_.length();

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);

  // Filter passes on the partial buffer, up to the last 4 bytes which it needs to resume the
  // decoder on the next call.
  std::string contents = bufferToString(buffer_);
  EXPECT_EQ(len - 4, buffer_.length());
  EXPECT_EQ(expected_contents.substr(0, len - 4), contents);

  buffer_.drain(buffer_.length());

  // Complete the buffer
  writePartialFramedBinaryMessage(buffer_, MessageType::Call, 0x10, false);
  expected_contents = expected_contents.substr(len - 4) + bufferToString(buffer_);
  len = buffer_.length();

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);

  // Filter buffered bytes from end of first buffer and passes them on now.
  contents = bufferToString(buffer_);
  EXPECT_EQ(len + 4, buffer_.length());
  EXPECT_EQ(expected_contents, contents);

  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
}

TEST_F(ThriftFilterTest, OnDataHandlesInvalidMsgType) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Reply, 0x0F); // reply is invalid for a request

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(1U, store_.counter("test.request_invalid_type").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(ThriftFilterTest, OnDataHandlesProtocolError) {
  initializeFilter();
  addSeq(buffer_, {
                      0x00, 0x00, 0x00, 0x1d,                     // framed: 29 bytes
                      0x80, 0x01, 0x00, 0xFF,                     // binary, illegal type
                      0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                      0x00, 0x00, 0x00, 0x01,                     // sequence id
                      0x00,                                       // struct stop field
                  });
  uint64_t len = buffer_.length();

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(len, buffer_.length());

  // Sniffing is now disabled.
  buffer_.drain(buffer_.length());
  writeFramedBinaryMessage(buffer_, MessageType::Oneway, 0x0F);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(0U, store_.counter("test.request").value());
}

TEST_F(ThriftFilterTest, OnDataHandlesProtocolErrorOnWrite) {
  initializeFilter();

  // Start the read buffer
  writePartialFramedBinaryMessage(buffer_, MessageType::Call, 0x10, true);
  uint64_t len = buffer_.length();

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  len -= buffer_.length();

  // Disable sniffing
  addSeq(write_buffer_, {
                            0x00, 0x00, 0x00, 0x1d,                     // framed: 29 bytes
                            0x80, 0x01, 0x00, 0xFF,                     // binary, illegal type
                            0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                            0x00, 0x00, 0x00, 0x01,                     // sequence id
                            0x00,                                       // struct stop field
                        });
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());

  // Complete the read buffer
  writePartialFramedBinaryMessage(buffer_, MessageType::Call, 0x10, false);
  len += buffer_.length();

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  len -= buffer_.length();
  EXPECT_EQ(0, len);
}

TEST_F(ThriftFilterTest, OnDataStopsSniffingWithTooManyPendingCalls) {
  initializeFilter();
  for (int i = 0; i < 64; i++) {
    writeFramedBinaryMessage(buffer_, MessageType::Call, i);
  }

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(64U, store_.gauge("test.request_active").value());
  buffer_.drain(buffer_.length());

  // Sniffing is now disabled.
  writeFramedBinaryMessage(buffer_, MessageType::Oneway, 100);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(64U, store_.gauge("test.request_active").value());
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
}

TEST_F(ThriftFilterTest, OnWriteHandlesThriftReply) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F); // set up request
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  writeFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x0F);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ThriftFilterTest, OnWriteHandlesOutOrOrderThriftReply) {
  initializeFilter();

  // set up two requests
  writeFramedBinaryMessage(buffer_, MessageType::Call, 1);
  writeFramedBinaryMessage(buffer_, MessageType::Call, 2);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(2U, store_.counter("test.request").value());
  EXPECT_EQ(2U, store_.gauge("test.request_active").value());

  writeFramedBinaryMessage(write_buffer_, MessageType::Reply, 2);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  write_buffer_.drain(write_buffer_.length());
  writeFramedBinaryMessage(write_buffer_, MessageType::Reply, 1);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  EXPECT_EQ(2U, store_.counter("test.response").value());
  EXPECT_EQ(2U, store_.counter("test.response_reply").value());
  EXPECT_EQ(2U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ThriftFilterTest, OnWriteHandlesFrameSplitAcrossBuffers) {
  initializeFilter();

  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F); // set up request
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);

  writePartialFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x0F, true);
  std::string expected_contents = bufferToString(write_buffer_);
  uint64_t len = write_buffer_.length();

  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  // Filter passes on the partial buffer, up to the last 4 bytes which it needs to resume the
  // decoder on the next call.
  std::string contents = bufferToString(write_buffer_);
  EXPECT_EQ(len - 4, write_buffer_.length());
  EXPECT_EQ(expected_contents.substr(0, len - 4), contents);

  write_buffer_.drain(write_buffer_.length());

  // Complete the buffer
  writePartialFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x0F, false);
  expected_contents = expected_contents.substr(len - 4) + bufferToString(write_buffer_);
  len = write_buffer_.length();

  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  // Filter buffered bytes from end of first buffer and passes them on now.
  contents = bufferToString(write_buffer_);
  EXPECT_EQ(len + 4, write_buffer_.length());
  EXPECT_EQ(expected_contents, contents);

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
}

TEST_F(ThriftFilterTest, OnWriteHandlesTApplicationException) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F); // set up request
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  writeFramedBinaryTApplicationException(write_buffer_, 0x0F);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ThriftFilterTest, OnWriteHandlesIDLException) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F); // set up request
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  writeFramedBinaryIDLException(write_buffer_, 0x0F);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(1U, store_.counter("test.response_error").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ThriftFilterTest, OnWriteHandlesInvalidMsgType) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  writeFramedBinaryMessage(write_buffer_, MessageType::Call, 0x0F); // call is invalid for response
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(1U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ThriftFilterTest, OnWriteHandlesProtocolError) {
  initializeFilter();
  addSeq(write_buffer_, {
                            0x00, 0x00, 0x00, 0x1d,                     // framed: 29 bytes
                            0x80, 0x01, 0x00, 0xFF,                     // binary, illegal type
                            0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                            0x00, 0x00, 0x00, 0x01,                     // sequence id
                            0x00,                                       // struct stop field
                        });
  uint64_t len = buffer_.length();

  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(len, buffer_.length());

  // Sniffing is now disabled.
  write_buffer_.drain(write_buffer_.length());
  writeFramedBinaryMessage(write_buffer_, MessageType::Reply, 1);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);
}

TEST_F(ThriftFilterTest, OnWriteHandlesProtocolErrorOnData) {
  initializeFilter();

  // Set up a request for the partial write
  writeFramedBinaryMessage(buffer_, MessageType::Call, 1);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  buffer_.drain(buffer_.length());

  // Start the write buffer
  writePartialFramedBinaryMessage(write_buffer_, MessageType::Reply, 1, true);
  uint64_t len = write_buffer_.length();

  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);
  len -= write_buffer_.length();

  // Force an error on the next request.
  addSeq(buffer_, {
                      0x00, 0x00, 0x00, 0x1d,                     // framed: 29 bytes
                      0x80, 0x01, 0x00, 0xFF,                     // binary, illegal type
                      0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                      0x00, 0x00, 0x00, 0x02,                     // sequence id
                      0x00,                                       // struct stop field
                  });
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());

  // Complete the read buffer
  writePartialFramedBinaryMessage(write_buffer_, MessageType::Reply, 1, false);
  len += write_buffer_.length();

  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);
  len -= write_buffer_.length();
  EXPECT_EQ(0, len);
}

TEST_F(ThriftFilterTest, OnEvent) {
  // No active calls
  {
    initializeFilter();
    filter_->onEvent(Network::ConnectionEvent::RemoteClose);
    filter_->onEvent(Network::ConnectionEvent::LocalClose);
    EXPECT_EQ(0U, store_.counter("test.cx_destroy_local_with_active_rq").value());
    EXPECT_EQ(0U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
  }

  // Close mid-request
  {
    initializeFilter();
    addSeq(buffer_, {
                        0x00, 0x00, 0x00, 0x1d,                     // framed: 29 bytes
                        0x80, 0x01, 0x00, 0x01,                     // binary proto, call type
                        0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                        0x00, 0x00, 0x00, 0x0F,                     // seq id
                    });
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);

    filter_->onEvent(Network::ConnectionEvent::RemoteClose);
    EXPECT_EQ(1U, store_.counter("test.cx_destroy_local_with_active_rq").value());

    filter_->onEvent(Network::ConnectionEvent::LocalClose);
    EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());

    buffer_.drain(buffer_.length());
  }

  // Close before response
  {
    initializeFilter();
    writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);

    filter_->onEvent(Network::ConnectionEvent::RemoteClose);
    EXPECT_EQ(1U, store_.counter("test.cx_destroy_local_with_active_rq").value());

    filter_->onEvent(Network::ConnectionEvent::LocalClose);
    EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());

    buffer_.drain(buffer_.length());
  }

  // Close mid-response
  {
    initializeFilter();
    writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);

    addSeq(write_buffer_, {
                              0x00, 0x00, 0x00, 0x1d, // framed: 29 bytes
                              0x80, 0x01, 0x00, 0x02, // binary proto, reply type
                              0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                              0x00, 0x00, 0x00, 0x0F,                     // seq id
                          });
    EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

    filter_->onEvent(Network::ConnectionEvent::RemoteClose);
    EXPECT_EQ(1U, store_.counter("test.cx_destroy_local_with_active_rq").value());

    filter_->onEvent(Network::ConnectionEvent::LocalClose);
    EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());

    buffer_.drain(buffer_.length());
    write_buffer_.drain(write_buffer_.length());
  }
}

TEST_F(ThriftFilterTest, ResponseWithUnknownSequenceID) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);

  writeFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x10);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
