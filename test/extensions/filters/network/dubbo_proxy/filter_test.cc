#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.h"
#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.validate.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/dubbo_proxy/buffer_helper.h"
#include "extensions/filters/network/dubbo_proxy/filter.h"

#include "test/extensions/filters/network/dubbo_proxy/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

using ConfigDubboProxy = envoy::extensions::filters::network::dubbo_proxy::v2alpha1::DubboProxy;

class DubboFilterTest : public testing::Test {
public:
  DubboFilterTest() {}

  TimeSource& timeSystem() { return factory_context_.dispatcher().timeSystem(); }

  void initializeFilter() {
    for (const auto& counter : store_.counters()) {
      counter->reset();
    }

    filter_ = std::make_unique<Filter>("test.", ConfigDubboProxy::Dubbo, ConfigDubboProxy::Hessian2,
                                       store_, timeSystem());
    filter_->initializeReadFilterCallbacks(read_filter_callbacks_);
    filter_->onNewConnection();

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  void writeHessianErrorResponseMessage(Buffer::Instance& buffer, bool is_event,
                                        int64_t request_id) {
    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x46});                   // Response status
    addInt64(buffer, request_id);                    // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x01}); // Body Length
  }

  void writeHessianExceptionResponseMessage(Buffer::Instance& buffer, bool is_event,
                                            int64_t request_id) {
    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x14});
    addInt64(buffer, request_id);                      // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x06,     // Body Length
                           '\x90',                     // return type, exception
                           0x05, 't', 'e', 's', 't'}); // return body
  }

  void writeInvalidResponseMessage(Buffer::Instance& buffer) {
    buffer.add(std::string{
        '\xda', '\xbb', 0x43, 0x14, // Response Message Header, illegal serialization id
        0x00,   0x00,   0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // Request Id
        0x00,   0x00,   0x00, 0x06,                         // Body Length
        '\x94',                                             // return type
        0x05,   't',    'e',  's',  't',                    // return body
    });
  }

  void writeInvalidRequestMessage(Buffer::Instance& buffer) {
    buffer.add(std::string{
        '\xda', '\xbb', '\xc3', 0x00, // Response Message Header, illegal serialization id
        0x00,   0x00,   0x00,   0x00, 0x00, 0x00, 0x00, 0x01, // Request Id
        0x00,   0x00,   0x00,   0x16,                         // Body Length
        0x05,   '2',    '.',    '0',  '.',  '2',              // Dubbo version
        0x04,   't',    'e',    's',  't',                    // Service naem
        0x05,   '0',    '.',    '0',  '.',  '0',              // Service version
        0x04,   't',    'e',    's',  't',                    // method name
    });
  }

  void writePartialHessianResponseMessage(Buffer::Instance& buffer, bool is_event,
                                          int64_t request_id, bool start) {

    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    if (start) {
      buffer.add(std::string{'\xda', '\xbb'});
      buffer.add(static_cast<void*>(&msg_type), 1);
      buffer.add(std::string{0x14});
      addInt64(buffer, request_id);                  // Request Id
      buffer.add(std::string{0x00, 0x00, 0x00, 0x06, // Body Length
                             '\x94'});               // return type, exception
    } else {
      buffer.add(std::string{0x05, 't', 'e', 's', 't'}); // return body
    }
  }

  void writeHessianResponseMessage(Buffer::Instance& buffer, bool is_event, int64_t request_id) {
    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x14});
    addInt64(buffer, request_id);                              // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x06,             // Body Length
                           '\x94', 0x05, 't', 'e', 's', 't'}); // return type, exception
  }

  void writePartialHessianRequestMessage(Buffer::Instance& buffer, bool is_one_way, bool is_event,
                                         int64_t request_id, bool start) {
    uint8_t msg_type = 0xc2; // request message, two_way, not event
    if (is_one_way) {
      msg_type = msg_type & 0xbf;
    }

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    if (start) {
      buffer.add(std::string{'\xda', '\xbb'});
      buffer.add(static_cast<void*>(&msg_type), 1);
      buffer.add(std::string{0x00});
      addInt64(buffer, request_id);                           // Request Id
      buffer.add(std::string{0x00, 0x00, 0x00, 0x16,          // Body Length
                             0x05, '2', '.', '0', '.', '2'}); // Dubbo version
    } else {
      buffer.add(std::string{
          0x04, 't', 'e', 's', 't',      // Service naem
          0x05, '0', '.', '0', '.', '0', // Service version
          0x04, 't', 'e', 's', 't',      // method name
      });
    }
  }

  void writeHessianRequestMessage(Buffer::Instance& buffer, bool is_one_way, bool is_event,
                                  int64_t request_id) {
    uint8_t msg_type = 0xc2; // request message, two_way, not event
    if (is_one_way) {
      msg_type = msg_type & 0xbf;
    }

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x00});
    addInt64(buffer, request_id);                            // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x16,           // Body Length
                           0x05, '2',  '.',  '0',  '.', '2', // Dubbo version
                           0x04, 't',  'e',  's',  't',      // Service naem
                           0x05, '0',  '.',  '0',  '.', '0', // Service version
                           0x04, 't',  'e',  's',  't'});    // method name
  }

  Buffer::OwnedImpl buffer_;
  Buffer::OwnedImpl write_buffer_;
  Stats::IsolatedStoreImpl store_;
  NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  std::unique_ptr<Filter> filter_;
};

TEST_F(DubboFilterTest, OnDataHandlesRequestTwoWay) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_twoway").value());
  EXPECT_EQ(0U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_event").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(DubboFilterTest, OnDataHandlesRequestOneWay) {
  initializeFilter();
  bool one_way = true;
  writeHessianRequestMessage(buffer_, one_way, false, 0x0F);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_twoway").value());
  EXPECT_EQ(1U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_event").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(DubboFilterTest, OnDataHandlesRequestEvent) {
  initializeFilter();
  bool event = true;
  writeHessianRequestMessage(buffer_, false, event, 0x0F);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_twoway").value());
  EXPECT_EQ(0U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(1U, store_.counter("test.request_event").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(DubboFilterTest, OnDataHandlesMessageSplitAcrossBuffers) {
  initializeFilter();
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, true);
  std::string expected_contents = buffer_.toString();
  uint64_t len = buffer_.length();

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);

  // Filter passes on the partial buffer, up to the last 6 bytes which it needs to resume the
  // decoder on the next call.
  std::string contents = buffer_.toString();
  EXPECT_EQ(16, buffer_.length());
  EXPECT_EQ(len - 6, buffer_.length());
  EXPECT_EQ(expected_contents.substr(0, len - 6), contents);
  buffer_.drain(buffer_.length());

  // Complete the buffer
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, false);
  expected_contents = expected_contents.substr(len - 6) + buffer_.toString();
  len = buffer_.length();

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);

  // Filter buffered bytes from end of first buffer and passes them on now.
  contents = buffer_.toString();
  EXPECT_EQ(len + 6, buffer_.length());
  EXPECT_EQ(expected_contents, contents);

  EXPECT_EQ(1U, store_.counter("test.request_twoway").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
}

TEST_F(DubboFilterTest, OnDataHandlesProtocolError) {
  initializeFilter();
  writeInvalidRequestMessage(buffer_);
  uint64_t len = buffer_.length();
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(len, buffer_.length());

  // Sniffing is now disabled.
  buffer_.drain(buffer_.length());
  bool one_way = true;
  writeHessianRequestMessage(buffer_, one_way, false, 0x0F);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(0U, store_.counter("test.request").value());
}

TEST_F(DubboFilterTest, OnDataHandlesProtocolErrorOnWrite) {
  initializeFilter();

  // Start the read buffer
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, true);
  uint64_t len = buffer_.length();

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  len -= buffer_.length();

  // Disable sniffing
  writeInvalidRequestMessage(write_buffer_);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());

  // Complete the read buffer
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, false);
  len += buffer_.length();

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  len -= buffer_.length();
  EXPECT_EQ(0, len);
}

TEST_F(DubboFilterTest, OnDataStopsSniffingWithTooManyPendingCalls) {
  initializeFilter();
  for (int i = 0; i < 64; i++) {
    writeHessianRequestMessage(buffer_, false, false, i);
  }

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(64U, store_.gauge("test.request_active").value());
  buffer_.drain(buffer_.length());

  // Sniffing is now disabled.
  writeInvalidRequestMessage(buffer_);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(64U, store_.gauge("test.request_active").value());
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
}

TEST_F(DubboFilterTest, OnWriteHandlesResponse) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  writeHessianResponseMessage(write_buffer_, false, 0x0F);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(DubboFilterTest, OnWriteHandlesOutOrOrderResponse) {
  initializeFilter();

  // set up two requests
  writeHessianRequestMessage(buffer_, false, false, 1);
  writeHessianRequestMessage(buffer_, false, false, 2);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(2U, store_.counter("test.request").value());
  EXPECT_EQ(2U, store_.gauge("test.request_active").value());

  writeHessianResponseMessage(write_buffer_, false, 2);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  write_buffer_.drain(write_buffer_.length());
  writeHessianResponseMessage(write_buffer_, false, 1);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  EXPECT_EQ(2U, store_.counter("test.response").value());
  EXPECT_EQ(2U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(DubboFilterTest, OnWriteHandlesFrameSplitAcrossBuffers) {
  initializeFilter();

  writeHessianRequestMessage(buffer_, false, false, 1);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);

  writePartialHessianResponseMessage(write_buffer_, false, 1, true);
  std::string expected_contents = write_buffer_.toString();
  uint64_t len = write_buffer_.length();

  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  // Filter passes on the partial buffer, up to the last 1 bytes which it needs to resume the
  // decoder on the next call.
  std::string contents = write_buffer_.toString();
  EXPECT_EQ(len - 1, write_buffer_.length());
  EXPECT_EQ(expected_contents.substr(0, len - 1), contents);

  write_buffer_.drain(write_buffer_.length());

  // Complete the buffer
  writePartialHessianResponseMessage(write_buffer_, false, 1, false);
  expected_contents = expected_contents.substr(len - 1) + write_buffer_.toString();
  len = write_buffer_.length();

  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  // Filter buffered bytes from end of first buffer and passes them on now.
  contents = write_buffer_.toString();
  EXPECT_EQ(len + 1, write_buffer_.length());
  EXPECT_EQ(expected_contents, contents);

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
}

TEST_F(DubboFilterTest, OnWriteHandlesResponseException) {
  initializeFilter();

  writeHessianRequestMessage(buffer_, false, false, 1);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  writeHessianExceptionResponseMessage(write_buffer_, false, 1);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(DubboFilterTest, OnWriteHandlesResponseError) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  writeHessianErrorResponseMessage(write_buffer_, false, 1);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(1U, store_.counter("test.response_error").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(DubboFilterTest, OnWriteHandlesProtocolError) {
  initializeFilter();
  writeInvalidResponseMessage(write_buffer_);
  uint64_t len = buffer_.length();
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(len, buffer_.length());

  // Sniffing is now disabled.
  write_buffer_.drain(write_buffer_.length());
  writeHessianResponseMessage(write_buffer_, false, 1);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);
}

TEST_F(DubboFilterTest, OnWriteHandlesProtocolErrorOnData) {
  initializeFilter();

  // Set up a request for the partial write
  writeHessianRequestMessage(buffer_, false, false, 1);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  buffer_.drain(buffer_.length());

  // Start the write buffer

  writePartialHessianResponseMessage(write_buffer_, false, 1, true);
  uint64_t len = write_buffer_.length();

  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);
  len -= write_buffer_.length();

  // Force an error on the next request.
  writeInvalidRequestMessage(buffer_);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());

  // Complete the read buffer
  writePartialHessianResponseMessage(write_buffer_, false, 1, false);
  len += write_buffer_.length();

  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);
  len -= write_buffer_.length();
  EXPECT_EQ(0, len);
}

TEST_F(DubboFilterTest, OnEvent) {
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
    writePartialHessianRequestMessage(buffer_, false, false, 1, true);
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
    writeHessianRequestMessage(buffer_, false, false, 1);
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
    writeHessianRequestMessage(buffer_, false, false, 1);
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);
    writePartialHessianResponseMessage(write_buffer_, false, 1, true);
    EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);

    filter_->onEvent(Network::ConnectionEvent::RemoteClose);
    EXPECT_EQ(0U, store_.counter("test.cx_destroy_local_with_active_rq").value());

    filter_->onEvent(Network::ConnectionEvent::LocalClose);
    EXPECT_EQ(0U, store_.counter("test.cx_destroy_remote_with_active_rq").value());

    buffer_.drain(buffer_.length());
    write_buffer_.drain(write_buffer_.length());
  }
}

TEST_F(DubboFilterTest, ResponseWithUnknownSequenceID) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::Continue);

  writeHessianResponseMessage(write_buffer_, false, 10);
  EXPECT_EQ(filter_->onWrite(write_buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy