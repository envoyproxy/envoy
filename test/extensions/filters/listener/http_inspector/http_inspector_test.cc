#include "common/common/hex.h"
#include "common/http/utility.h"
#include "common/network/io_socket_handle_impl.h"

#include "extensions/filters/listener/http_inspector/http_inspector.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {
namespace {

class HttpInspectorTest : public testing::Test {
public:
  HttpInspectorTest()
      : cfg_(std::make_shared<Config>(store_)),
        io_handle_(std::make_unique<Network::IoSocketHandleImpl>(42)) {}
  ~HttpInspectorTest() override { io_handle_->close(); }

  void init(bool include_inline_recv = true) {
    filter_ = std::make_unique<Filter>(cfg_);

    EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
    EXPECT_CALL(socket_, detectedTransportProtocol()).WillRepeatedly(Return("raw_buffer"));
    EXPECT_CALL(cb_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(testing::Const(socket_), ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));

    if (include_inline_recv) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Return(Api::SysCallSizeResult{static_cast<ssize_t>(0), 0}));

      EXPECT_CALL(dispatcher_,
                  createFileEvent_(_, _, Event::FileTriggerType::Edge,
                                   Event::FileReadyType::Read | Event::FileReadyType::Closed))
          .WillOnce(DoAll(SaveArg<1>(&file_event_callback_),
                          ReturnNew<NiceMock<Event::MockFileEvent>>()));

      filter_->onAccept(cb_);
    }
  }

  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
  Stats::IsolatedStoreImpl store_;
  ConfigSharedPtr cfg_;
  std::unique_ptr<Filter> filter_;
  Network::MockListenerFilterCallbacks cb_;
  Network::MockConnectionSocket socket_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::FileReadyCb file_event_callback_;
  Network::IoHandlePtr io_handle_;
};

TEST_F(HttpInspectorTest, SkipHttpInspectForTLS) {
  filter_ = std::make_unique<Filter>(cfg_);

  EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
  EXPECT_CALL(socket_, detectedTransportProtocol()).WillRepeatedly(Return("TLS"));
  EXPECT_EQ(filter_->onAccept(cb_), Network::FilterStatus::Continue);
}

TEST_F(HttpInspectorTest, InlineReadIoError) {
  init(/*include_inline_recv=*/false);
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke([](os_fd_t, void*, size_t, int) -> Api::SysCallSizeResult {
        return Api::SysCallSizeResult{ssize_t(-1), 0};
      }));
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(socket_, close()).Times(1);
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
  // It's arguable if io error should bump the not_found counter
  EXPECT_EQ(0, cfg_->stats().http_not_found_.value());
}

TEST_F(HttpInspectorTest, InlineReadInspectHttp10) {
  init(/*include_inline_recv=*/false);
  const absl::string_view header =
      "GET /anything HTTP/1.0\r\nhost: google.com\r\nuser-agent: curl/7.64.0\r\naccept: "
      "*/*\r\nx-forwarded-proto: http\r\nx-request-id: "
      "a52df4a0-ed00-4a19-86a7-80e5049c6c84\r\nx-envoy-expected-rq-timeout-ms: "
      "15000\r\ncontent-length: 0\r\n\r\n";
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= header.size());
            memcpy(buffer, header.data(), header.size());
            return Api::SysCallSizeResult{ssize_t(header.size()), 0};
          }));
  const std::vector<absl::string_view> alpn_protos{Http::Utility::AlpnNames::get().Http10};

  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);

  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::Continue);
  EXPECT_EQ(1, cfg_->stats().http10_found_.value());
}

TEST_F(HttpInspectorTest, InlineReadParseError) {
  init(/*include_inline_recv=*/false);
  const absl::string_view header =
      "NOT_A_LEGAL_PREFIX /anything HTTP/1.0\r\nhost: google.com\r\nuser-agent: "
      "curl/7.64.0\r\naccept: "
      "*/*\r\nx-forwarded-proto: http\r\nx-request-id: "
      "a52df4a0-ed00-4a19-86a7-80e5049c6c84\r\nx-envoy-expected-rq-timeout-ms: "
      "15000\r\ncontent-length: 0\r\n\r\n";
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= header.size());
            memcpy(buffer, header.data(), header.size());
            return Api::SysCallSizeResult{ssize_t(header.size()), 0};
          }));
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::Continue);
  EXPECT_EQ(1, cfg_->stats().http_not_found_.value());
}

TEST_F(HttpInspectorTest, InspectHttp10) {
  init(true);
  const absl::string_view header =
      "GET /anything HTTP/1.0\r\nhost: google.com\r\nuser-agent: curl/7.64.0\r\naccept: "
      "*/*\r\nx-forwarded-proto: http\r\nx-request-id: "
      "a52df4a0-ed00-4a19-86a7-80e5049c6c84\r\nx-envoy-expected-rq-timeout-ms: "
      "15000\r\ncontent-length: 0\r\n\r\n";

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= header.size());
            memcpy(buffer, header.data(), header.size());
            return Api::SysCallSizeResult{ssize_t(header.size()), 0};
          }));

  const std::vector<absl::string_view> alpn_protos{Http::Utility::AlpnNames::get().Http10};

  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().http10_found_.value());
}

TEST_F(HttpInspectorTest, InspectHttp11) {
  init();
  const absl::string_view header =
      "GET /anything HTTP/1.1\r\nhost: google.com\r\nuser-agent: curl/7.64.0\r\naccept: "
      "*/*\r\nx-forwarded-proto: http\r\nx-request-id: "
      "a52df4a0-ed00-4a19-86a7-80e5049c6c84\r\nx-envoy-expected-rq-timeout-ms: "
      "15000\r\ncontent-length: 0\r\n\r\n";

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= header.size());
            memcpy(buffer, header.data(), header.size());
            return Api::SysCallSizeResult{ssize_t(header.size()), 0};
          }));

  const std::vector<absl::string_view> alpn_protos{Http::Utility::AlpnNames::get().Http11};

  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().http11_found_.value());
}

TEST_F(HttpInspectorTest, InspectHttp11WithNonEmptyRequestBody) {
  init();
  const absl::string_view header =
      "GET /anything HTTP/1.1\r\nhost: google.com\r\nuser-agent: curl/7.64.0\r\naccept: "
      "*/*\r\nx-forwarded-proto: http\r\nx-request-id: "
      "a52df4a0-ed00-4a19-86a7-80e5049c6c84\r\nx-envoy-expected-rq-timeout-ms: "
      "15000\r\ncontent-length: 3\r\n\r\nfoo";

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= header.size());
            memcpy(buffer, header.data(), header.size());
            return Api::SysCallSizeResult{ssize_t(header.size()), 0};
          }));

  const std::vector<absl::string_view> alpn_protos{Http::Utility::AlpnNames::get().Http11};

  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().http11_found_.value());
}

TEST_F(HttpInspectorTest, ExtraSpaceInRequestLine) {
  init();
  const absl::string_view header = "GET  /anything  HTTP/1.1\r\n\r\n";
  //                                   ^^         ^^

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= header.size());
            memcpy(buffer, header.data(), header.size());
            return Api::SysCallSizeResult{ssize_t(header.size()), 0};
          }));

  const std::vector<absl::string_view> alpn_protos{Http::Utility::AlpnNames::get().Http11};

  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().http11_found_.value());
}

TEST_F(HttpInspectorTest, InvalidHttpMethod) {
  init();
  const absl::string_view header = "BAD /anything HTTP/1.1";

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= header.size());
            memcpy(buffer, header.data(), header.size());
            return Api::SysCallSizeResult{ssize_t(header.size()), 0};
          }));

  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(0, cfg_->stats().http11_found_.value());
}

TEST_F(HttpInspectorTest, InvalidHttpRequestLine) {
  init();
  const absl::string_view header = "BAD /anything HTTP/1.1\r\n";

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= header.size());
            memcpy(buffer, header.data(), header.size());
            return Api::SysCallSizeResult{ssize_t(header.size()), 0};
          }));

  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(_));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().http_not_found_.value());
}

TEST_F(HttpInspectorTest, OldHttpProtocol) {
  init();
  const absl::string_view header = "GET /anything HTTP/0.9\r\n";

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= header.size());
            memcpy(buffer, header.data(), header.size());
            return Api::SysCallSizeResult{ssize_t(header.size()), 0};
          }));

  const std::vector<absl::string_view> alpn_protos{Http::Utility::AlpnNames::get().Http10};
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().http10_found_.value());
}

TEST_F(HttpInspectorTest, InvalidRequestLine) {
  init();
  const absl::string_view header = "GET /anything HTTP/1.1 BadRequestLine\r\n";

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= header.size());
            memcpy(buffer, header.data(), header.size());
            return Api::SysCallSizeResult{ssize_t(header.size()), 0};
          }));

  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().http_not_found_.value());
}

TEST_F(HttpInspectorTest, InspectHttp2) {
  init();

  const std::string header =
      "505249202a20485454502f322e300d0a0d0a534d0d0a0d0a00000c04000000000000041000000000020000000000"
      "00040800000000000fff000100007d010500000001418aa0e41d139d09b8f0000f048860757a4ce6aa660582867a"
      "8825b650c3abb8d2e053032a2f2a408df2b4a7b3c0ec90b22d5d8749ff839d29af4089f2b585ed6950958d279a18"
      "9e03f1ca5582265f59a75b0ac3111959c7e49004908db6e83f4096f2b16aee7f4b17cd65224b22d6765926a4a7b5"
      "2b528f840b60003f";
  std::vector<uint8_t> data = Hex::decode(header);

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));

  const std::vector<absl::string_view> alpn_protos{Http::Utility::AlpnNames::get().Http2c};

  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().http2_found_.value());
}

TEST_F(HttpInspectorTest, InvalidConnectionPreface) {
  init();

  const std::string header = "505249202a20485454502f322e300d0a";
  const std::vector<uint8_t> data = Hex::decode(header);

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));

  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(true)).Times(0);
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(0, cfg_->stats().http_not_found_.value());
}

TEST_F(HttpInspectorTest, ReadError) {
  init();

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
    return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_NOT_SUP};
  }));
  EXPECT_CALL(cb_, continueFilterChain(false));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().read_error_.value());
}

TEST_F(HttpInspectorTest, MultipleReadsHttp2) {
  init();
  const std::vector<absl::string_view> alpn_protos{Http::Utility::AlpnNames::get().Http2c};

  const std::string header =
      "505249202a20485454502f322e300d0a0d0a534d0d0a0d0a00000c04000000000000041000000000020000000000"
      "00040800000000000fff000100007d010500000001418aa0e41d139d09b8f0000f048860757a4ce6aa660582867a"
      "8825b650c3abb8d2e053032a2f2a408df2b4a7b3c0ec90b22d5d8749ff839d29af4089f2b585ed6950958d279a18"
      "9e03f1ca5582265f59a75b0ac3111959c7e49004908db6e83f4096f2b16aee7f4b17cd65224b22d6765926a4a7b5"
      "2b528f840b60003f";
  const std::vector<uint8_t> data = Hex::decode(header);
  {
    InSequence s;

    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
      return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
    }));

    for (size_t i = 1; i <= 24; i++) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke(
              [&data, i](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= i);
                memcpy(buffer, data.data(), i);
                return Api::SysCallSizeResult{ssize_t(i), 0};
              }));
    }
  }

  bool got_continue = false;
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(cb_, continueFilterChain(true)).WillOnce(InvokeWithoutArgs([&got_continue]() {
    got_continue = true;
  }));
  while (!got_continue) {
    file_event_callback_(Event::FileReadyType::Read);
  }
  EXPECT_EQ(1, cfg_->stats().http2_found_.value());
}

TEST_F(HttpInspectorTest, MultipleReadsHttp2BadPreface) {
  init();
  const std::string header = "505249202a20485454502f322e300d0a0d0c";
  const std::vector<uint8_t> data = Hex::decode(header);
  {
    InSequence s;

    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
      return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
    }));

    for (size_t i = 1; i <= data.size(); i++) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke(
              [&data, i](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= i);
                memcpy(buffer, data.data(), i);
                return Api::SysCallSizeResult{ssize_t(i), 0};
              }));
    }
  }

  bool got_continue = false;
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(true)).WillOnce(InvokeWithoutArgs([&got_continue]() {
    got_continue = true;
  }));
  while (!got_continue) {
    file_event_callback_(Event::FileReadyType::Read);
  }
  EXPECT_EQ(1, cfg_->stats().http_not_found_.value());
}

TEST_F(HttpInspectorTest, MultipleReadsHttp1) {
  init();
  const absl::string_view data = "GET /anything HTTP/1.0\r";
  {
    InSequence s;

    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
      return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
    }));

    for (size_t i = 1; i <= data.size(); i++) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke(
              [&data, i](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= i);
                memcpy(buffer, data.data(), i);
                return Api::SysCallSizeResult{ssize_t(i), 0};
              }));
    }
  }

  bool got_continue = false;
  const std::vector<absl::string_view> alpn_protos{Http::Utility::AlpnNames::get().Http10};
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(cb_, continueFilterChain(true)).WillOnce(InvokeWithoutArgs([&got_continue]() {
    got_continue = true;
  }));
  while (!got_continue) {
    file_event_callback_(Event::FileReadyType::Read);
  }
  EXPECT_EQ(1, cfg_->stats().http10_found_.value());
}

TEST_F(HttpInspectorTest, MultipleReadsHttp1IncompleteHeader) {
  init();
  const absl::string_view data = "GE";
  bool end_stream = false;
  {
    InSequence s;

    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
      return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
    }));

    for (size_t i = 1; i <= data.size(); i++) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke([&data, &end_stream, i](os_fd_t, void* buffer, size_t length,
                                                   int) -> Api::SysCallSizeResult {
            ASSERT(length >= i);
            memcpy(buffer, data.data(), i);
            if (i == data.size()) {
              end_stream = true;
            }

            return Api::SysCallSizeResult{ssize_t(i), 0};
          }));
    }
  }

  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_EQ(0, cfg_->stats().http_not_found_.value());
  while (!end_stream) {
    file_event_callback_(Event::FileReadyType::Read);
  }
}

TEST_F(HttpInspectorTest, MultipleReadsHttp1IncompleteBadHeader) {
  init();
  const absl::string_view data = "X";
  {
    InSequence s;

    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
      return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
    }));

    for (size_t i = 1; i <= data.size(); i++) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke(
              [&data, i](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= i);
                memcpy(buffer, data.data(), i);
                return Api::SysCallSizeResult{ssize_t(i), 0};
              }));
    }
  }

  bool got_continue = false;
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(true)).WillOnce(InvokeWithoutArgs([&got_continue]() {
    got_continue = true;
  }));
  while (!got_continue) {
    file_event_callback_(Event::FileReadyType::Read);
  }
  EXPECT_EQ(1, cfg_->stats().http_not_found_.value());
}

TEST_F(HttpInspectorTest, MultipleReadsHttp1BadProtocol) {
  init();
  const std::string valid_header = "GET /index HTTP/1.1\r";
  //  offset:                       0         10
  const std::string truncate_header = valid_header.substr(0, 14).append("\r");
  {
    InSequence s;

    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
      return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
    }));

    for (size_t i = 1; i <= truncate_header.size(); i++) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke([&truncate_header, i](os_fd_t, void* buffer, size_t length,
                                                 int) -> Api::SysCallSizeResult {
            ASSERT(length >= truncate_header.size());
            memcpy(buffer, truncate_header.data(), truncate_header.size());
            return Api::SysCallSizeResult{ssize_t(i), 0};
          }));
    }
  }

  bool got_continue = false;
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(true)).WillOnce(InvokeWithoutArgs([&got_continue]() {
    got_continue = true;
  }));
  while (!got_continue) {
    file_event_callback_(Event::FileReadyType::Read);
  }
  EXPECT_EQ(1, cfg_->stats().http_not_found_.value());
}

TEST_F(HttpInspectorTest, Http1WithLargeRequestLine) {
  init();
  absl::string_view method = "GET", http = "/index HTTP/1.0\r";
  std::string spaces(Config::MAX_INSPECT_SIZE - method.size() - http.size(), ' ');
  const std::string data = absl::StrCat(method, spaces, http);
  {
    InSequence s;

    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
      return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
    }));

    uint64_t num_loops = Config::MAX_INSPECT_SIZE;
#if defined(__has_feature) &&                                                                      \
    ((__has_feature(thread_sanitizer)) || (__has_feature(address_sanitizer)))
    num_loops = 2;
#endif

    for (size_t i = 1; i <= num_loops; i++) {
      size_t len = i;
      if (num_loops == 2) {
        len = size_t(Config::MAX_INSPECT_SIZE / (3 - i));
      }
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke(
              [&data, len](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= len);
                memcpy(buffer, data.data(), len);
                return Api::SysCallSizeResult{ssize_t(len), 0};
              }));
    }
  }

  bool got_continue = false;
  const std::vector<absl::string_view> alpn_protos{Http::Utility::AlpnNames::get().Http10};
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(cb_, continueFilterChain(true)).WillOnce(InvokeWithoutArgs([&got_continue]() {
    got_continue = true;
  }));
  while (!got_continue) {
    file_event_callback_(Event::FileReadyType::Read);
  }
  EXPECT_EQ(1, cfg_->stats().http10_found_.value());
}

TEST_F(HttpInspectorTest, Http1WithLargeHeader) {
  init();
  absl::string_view request = "GET /index HTTP/1.0\rfield: ";
  //                           0                  20
  std::string value(Config::MAX_INSPECT_SIZE - request.size(), 'a');
  const std::string data = absl::StrCat(request, value);
  {
    InSequence s;

    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
      return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
    }));

    for (size_t i = 1; i <= 20; i++) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke(
              [&data, i](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= data.size());
                memcpy(buffer, data.data(), i);
                return Api::SysCallSizeResult{ssize_t(i), 0};
              }));
    }
  }

  bool got_continue = false;
  const std::vector<absl::string_view> alpn_protos{Http::Utility::AlpnNames::get().Http10};
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(cb_, continueFilterChain(true)).WillOnce(InvokeWithoutArgs([&got_continue]() {
    got_continue = true;
  }));
  while (!got_continue) {
    file_event_callback_(Event::FileReadyType::Read);
  }
  EXPECT_EQ(1, cfg_->stats().http10_found_.value());
}

} // namespace
} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
