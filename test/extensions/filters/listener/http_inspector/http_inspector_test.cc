#include "source/common/common/hex.h"
#include "source/common/http/utility.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/listener_filter_buffer_impl.h"
#include "source/extensions/filters/listener/http_inspector/http_inspector.h"

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
        io_handle_(
            Network::SocketInterfaceImpl::makePlatformSpecificSocket(42, false, absl::nullopt)) {}
  ~HttpInspectorTest() override { io_handle_->close(); }

  void init() {
    filter_ = std::make_unique<Filter>(cfg_);

    EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
    EXPECT_CALL(socket_, detectedTransportProtocol()).WillRepeatedly(Return("raw_buffer"));
    EXPECT_CALL(cb_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(testing::Const(socket_), ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));
    EXPECT_CALL(socket_, ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));
    EXPECT_CALL(dispatcher_,
                createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                 Event::FileReadyType::Read | Event::FileReadyType::Closed))
        .WillOnce(
            DoAll(SaveArg<1>(&file_event_callback_), ReturnNew<NiceMock<Event::MockFileEvent>>()));
    buffer_ = std::make_unique<Network::ListenerFilterBufferImpl>(
        *io_handle_, dispatcher_, [](bool) {}, [](Network::ListenerFilterBuffer&) {},
        filter_->maxReadBytes());
  }

  void testHttpInspectMultipleReadsNotFound(absl::string_view header, bool http2 = false) {
    init();
    const std::vector<uint8_t> data = Hex::decode(std::string(header));
    {
      InSequence s;

#ifdef WIN32
      EXPECT_CALL(os_sys_calls_, readv(_, _, _))
          .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
      if (http2) {
        for (size_t i = 0; i < data.size(); i++) {
          EXPECT_CALL(os_sys_calls_, readv(_, _, _))
              .WillOnce(Invoke(
                  [&data, i](os_fd_t fd, const iovec* iov, int iovcnt) -> Api::SysCallSizeResult {
                    ASSERT(iov->iov_len >= data.size());
                    memcpy(iov->iov_base, data.data() + i, 1);
                    return Api::SysCallSizeResult{ssize_t(1), 0};
                  }))
              .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
        }
      } else {
        for (size_t i = 0; i < header.size(); i++) {
          EXPECT_CALL(os_sys_calls_, readv(_, _, _))
              .WillOnce(Invoke(
                  [&header, i](os_fd_t fd, const iovec* iov, int iovcnt) -> Api::SysCallSizeResult {
                    ASSERT(iov->iov_len >= header.size());
                    memcpy(iov->iov_base, header.data() + i, 1);
                    return Api::SysCallSizeResult{ssize_t(1), 0};
                  }))
              .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
        }
      }
#else
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
        return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
      }));
      if (http2) {
        for (size_t i = 1; i <= data.size(); i++) {
          EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
              .WillOnce(Invoke(
                  [&data, i](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                    ASSERT(length >= i);
                    memcpy(buffer, data.data(), i);
                    return Api::SysCallSizeResult{ssize_t(i), 0};
                  }));
        }
      } else {
        for (size_t i = 1; i <= header.size(); i++) {
          EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
              .WillOnce(Invoke([&header, i](os_fd_t, void* buffer, size_t length,
                                            int) -> Api::SysCallSizeResult {
                ASSERT(length >= i);
                memcpy(buffer, header.data(), i);
                return Api::SysCallSizeResult{ssize_t(i), 0};
              }));
        }
      }
#endif
    }
    bool got_continue = false;
    EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
    auto accepted = filter_->onAccept(cb_);
    EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
    while (!got_continue) {
      file_event_callback_(Event::FileReadyType::Read);
      auto status = filter_->onData(*buffer_);
      if (status == Network::FilterStatus::Continue) {
        got_continue = true;
      }
    }

    EXPECT_EQ(1, cfg_->stats().http_not_found_.value());
  }

  void testHttpInspectMultipleReadsFound(absl::string_view header, absl::string_view alpn) {
    init();
    const std::vector<absl::string_view> alpn_protos{alpn};
    const std::vector<uint8_t> data = Hex::decode(std::string(header));
    {
      InSequence s;

#ifdef WIN32
      EXPECT_CALL(os_sys_calls_, readv(_, _, _))
          .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
      if (alpn == Http::Utility::AlpnNames::get().Http2c) {
        for (size_t i = 0; i < 24; i++) {
          EXPECT_CALL(os_sys_calls_, readv(_, _, _))
              .WillOnce(Invoke(
                  [&data, i](os_fd_t fd, const iovec* iov, int iovcnt) -> Api::SysCallSizeResult {
                    ASSERT(iov->iov_len >= data.size());
                    memcpy(iov->iov_base, data.data() + i, 1);
                    return Api::SysCallSizeResult{ssize_t(1), 0};
                  }))
              .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
        }
      } else {
        for (size_t i = 0; i < header.size(); i++) {
          EXPECT_CALL(os_sys_calls_, readv(_, _, _))
              .WillOnce(Invoke(
                  [&header, i](os_fd_t fd, const iovec* iov, int iovcnt) -> Api::SysCallSizeResult {
                    ASSERT(iov->iov_len >= header.size());
                    memcpy(iov->iov_base, header.data() + i, 1);
                    return Api::SysCallSizeResult{ssize_t(1), 0};
                  }))
              .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
        }
      }
#else
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
        return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
      }));

      if (alpn == Http::Utility::AlpnNames::get().Http2c) {
        for (size_t i = 0; i <= 24; i++) {
          EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
              .WillOnce(Invoke(
                  [&data, i](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                    ASSERT(length >= i);
                    memcpy(buffer, data.data(), i);
                    return Api::SysCallSizeResult{ssize_t(i), 0};
                  }));
        }
      } else {
        for (size_t i = 0; i <= header.size(); i++) {
          EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
              .WillOnce(Invoke([&header, i](os_fd_t, void* buffer, size_t length,
                                            int) -> Api::SysCallSizeResult {
                ASSERT(length >= i);
                memcpy(buffer, header.data(), i);
                return Api::SysCallSizeResult{ssize_t(i), 0};
              }));
        }
      }
#endif
    }

    bool got_continue = false;
    EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
    auto accepted = filter_->onAccept(cb_);
    EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
    while (!got_continue) {
      file_event_callback_(Event::FileReadyType::Read);
      auto status = filter_->onData(*buffer_);
      if (status == Network::FilterStatus::Continue) {
        got_continue = true;
      }
    }
    if (alpn == Http::Utility::AlpnNames::get().Http11) {
      EXPECT_EQ(1, cfg_->stats().http11_found_.value());
    } else if (alpn == Http::Utility::AlpnNames::get().Http10) {
      EXPECT_EQ(1, cfg_->stats().http10_found_.value());
    } else if (alpn == Http::Utility::AlpnNames::get().Http2c) {
      EXPECT_EQ(1, cfg_->stats().http2_found_.value());
    } else {
      EXPECT_EQ(alpn, "unknow alpn");
    }
  }

  void testHttpInspectFound(absl::string_view header, absl::string_view alpn) {
    init();
    std::vector<uint8_t> data = Hex::decode(std::string(header));
#ifdef WIN32
    if (alpn == Http::Utility::AlpnNames::get().Http2c) {
      EXPECT_CALL(os_sys_calls_, readv(_, _, _))
          .WillOnce(
              Invoke([&data](os_fd_t fd, const iovec* iov, int iovcnt) -> Api::SysCallSizeResult {
                ASSERT(iov->iov_len >= data.size());
                memcpy(iov->iov_base, data.data(), data.size());
                return Api::SysCallSizeResult{ssize_t(data.size()), 0};
              }))
          .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
    } else {
      EXPECT_CALL(os_sys_calls_, readv(_, _, _))
          .WillOnce(
              Invoke([&header](os_fd_t fd, const iovec* iov, int iovcnt) -> Api::SysCallSizeResult {
                ASSERT(iov->iov_len >= header.size());
                memcpy(iov->iov_base, header.data(), header.size());
                return Api::SysCallSizeResult{ssize_t(header.size()), 0};
              }))
          .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
    }
#else
    if (alpn == Http::Utility::AlpnNames::get().Http2c) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(
              Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= data.size());
                memcpy(buffer, data.data(), data.size());
                return Api::SysCallSizeResult{ssize_t(data.size()), 0};
              }));
    } else {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke(
              [&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= header.size());
                memcpy(buffer, header.data(), header.size());
                return Api::SysCallSizeResult{ssize_t(header.size()), 0};
              }));
    }
#endif
    const std::vector<absl::string_view> alpn_protos{alpn};

    EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
    auto accepted = filter_->onAccept(cb_);
    EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
    file_event_callback_(Event::FileReadyType::Read);
    auto status = filter_->onData(*buffer_);
    EXPECT_EQ(status, Network::FilterStatus::Continue);
    if (alpn == Http::Utility::AlpnNames::get().Http11) {
      EXPECT_EQ(1, cfg_->stats().http11_found_.value());
    } else if (alpn == Http::Utility::AlpnNames::get().Http10) {
      EXPECT_EQ(1, cfg_->stats().http10_found_.value());
    } else if (alpn == Http::Utility::AlpnNames::get().Http2c) {
      EXPECT_EQ(1, cfg_->stats().http2_found_.value());
    } else {
      EXPECT_EQ(alpn, "unknow alpn");
    }
  }

  void testHttpInspectNotFound(absl::string_view header, bool http2 = false) {
    init();
    std::vector<uint8_t> data = Hex::decode(std::string(header));
#ifdef WIN32
    if (http2) {
      EXPECT_CALL(os_sys_calls_, readv(_, _, _))
          .WillOnce(
              Invoke([&data](os_fd_t fd, const iovec* iov, int iovcnt) -> Api::SysCallSizeResult {
                ASSERT(iov->iov_len >= data.size());
                memcpy(iov->iov_base, data.data(), data.size());
                return Api::SysCallSizeResult{ssize_t(data.size()), 0};
              }))
          .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
    } else {
      EXPECT_CALL(os_sys_calls_, readv(_, _, _))
          .WillOnce(
              Invoke([&header](os_fd_t fd, const iovec* iov, int iovcnt) -> Api::SysCallSizeResult {
                ASSERT(iov->iov_len >= header.size());
                memcpy(iov->iov_base, header.data(), header.size());
                return Api::SysCallSizeResult{ssize_t(header.size()), 0};
              }))
          .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
    }
#else
    if (http2) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(
              Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= data.size());
                memcpy(buffer, data.data(), data.size());
                return Api::SysCallSizeResult{ssize_t(data.size()), 0};
              }));
    } else {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke(
              [&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= header.size());
                memcpy(buffer, header.data(), header.size());
                return Api::SysCallSizeResult{ssize_t(header.size()), 0};
              }));
    }
#endif
    EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
    auto accepted = filter_->onAccept(cb_);
    EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
    file_event_callback_(Event::FileReadyType::Read);
    auto status = filter_->onData(*buffer_);
    EXPECT_EQ(status, Network::FilterStatus::Continue);
    EXPECT_EQ(1, cfg_->stats().http_not_found_.value());
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
  std::unique_ptr<Network::ListenerFilterBufferImpl> buffer_;
};

TEST_F(HttpInspectorTest, SkipHttpInspectForTLS) {
  filter_ = std::make_unique<Filter>(cfg_);

  EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
  EXPECT_CALL(socket_, ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));
  EXPECT_CALL(socket_, detectedTransportProtocol()).WillRepeatedly(Return("TLS"));
  EXPECT_EQ(filter_->onAccept(cb_), Network::FilterStatus::Continue);
}

TEST_F(HttpInspectorTest, InlineReadInspectHttp10) {
  const absl::string_view header =
      "GET /anything HTTP/1.0\r\nhost: google.com\r\nuser-agent: curl/7.64.0\r\naccept: "
      "*/*\r\nx-forwarded-proto: http\r\nx-request-id: "
      "a52df4a0-ed00-4a19-86a7-80e5049c6c84\r\nx-envoy-expected-rq-timeout-ms: "
      "15000\r\ncontent-length: 0\r\n\r\n";
  testHttpInspectFound(header, Http::Utility::AlpnNames::get().Http10);
}

TEST_F(HttpInspectorTest, InlineReadParseError) {
  const absl::string_view header =
      "NOT_A_LEGAL_PREFIX /anything HTTP/1.0\r\nhost: google.com\r\nuser-agent: "
      "curl/7.64.0\r\naccept: "
      "*/*\r\nx-forwarded-proto: http\r\nx-request-id: "
      "a52df4a0-ed00-4a19-86a7-80e5049c6c84\r\nx-envoy-expected-rq-timeout-ms: "
      "15000\r\ncontent-length: 0\r\n\r\n";
  testHttpInspectNotFound(header);
}

TEST_F(HttpInspectorTest, InspectHttp10) {
  const absl::string_view header =
      "GET /anything HTTP/1.0\r\nhost: google.com\r\nuser-agent: curl/7.64.0\r\naccept: "
      "*/*\r\nx-forwarded-proto: http\r\nx-request-id: "
      "a52df4a0-ed00-4a19-86a7-80e5049c6c84\r\nx-envoy-expected-rq-timeout-ms: "
      "15000\r\ncontent-length: 0\r\n\r\n";
  testHttpInspectFound(header, Http::Utility::AlpnNames::get().Http10);
}

TEST_F(HttpInspectorTest, InspectHttp11) {
  const absl::string_view header =
      "GET /anything HTTP/1.1\r\nhost: google.com\r\nuser-agent: curl/7.64.0\r\naccept: "
      "*/*\r\nx-forwarded-proto: http\r\nx-request-id: "
      "a52df4a0-ed00-4a19-86a7-80e5049c6c84\r\nx-envoy-expected-rq-timeout-ms: "
      "15000\r\ncontent-length: 0\r\n\r\n";
  testHttpInspectFound(header, Http::Utility::AlpnNames::get().Http11);
}

TEST_F(HttpInspectorTest, InspectHttp11WithNonEmptyRequestBody) {
  const absl::string_view header =
      "GET /anything HTTP/1.1\r\nhost: google.com\r\nuser-agent: curl/7.64.0\r\naccept: "
      "*/*\r\nx-forwarded-proto: http\r\nx-request-id: "
      "a52df4a0-ed00-4a19-86a7-80e5049c6c84\r\nx-envoy-expected-rq-timeout-ms: "
      "15000\r\ncontent-length: 3\r\n\r\nfoo";
  testHttpInspectFound(header, Http::Utility::AlpnNames::get().Http11);
}

TEST_F(HttpInspectorTest, ExtraSpaceInRequestLine) {
  const absl::string_view header = "GET  /anything  HTTP/1.1\r\n\r\n";
  testHttpInspectFound(header, Http::Utility::AlpnNames::get().Http11);
}

TEST_F(HttpInspectorTest, InvalidHttpMethod) {
  const absl::string_view header = "BAD /anything HTTP/1.1";
  testHttpInspectNotFound(header);
}

TEST_F(HttpInspectorTest, InvalidHttpRequestLine) {
  const absl::string_view header = "BAD /anything HTTP/1.1\r\n";
  testHttpInspectNotFound(header);
}

TEST_F(HttpInspectorTest, OldHttpProtocol) {
  const absl::string_view header = "GET /anything HTTP/0.9\r\n";
  testHttpInspectFound(header, Http::Utility::AlpnNames::get().Http10);
}

TEST_F(HttpInspectorTest, InvalidRequestLine) {
  const absl::string_view header = "GET /anything HTTP/1.1 BadRequestLine\r\n";
  testHttpInspectNotFound(header);
}

TEST_F(HttpInspectorTest, InvalidRequestLine2) {
  const absl::string_view header = "\r\n\r\n\r\n";
  testHttpInspectNotFound(header);
}

TEST_F(HttpInspectorTest, InvalidRequestLine3) {
  const absl::string_view header = "\r\n\r\n\r\n BAD";
  testHttpInspectNotFound(header);
}

TEST_F(HttpInspectorTest, InvalidRequestLine4) {
  const absl::string_view header = "\r\nGET /anything HTTP/1.1\r\n";
  testHttpInspectNotFound(header);
}

TEST_F(HttpInspectorTest, InspectHttp2) {
  const std::string header =
      "505249202a20485454502f322e300d0a0d0a534d0d0a0d0a00000c04000000000000041000000000020000000000"
      "00040800000000000fff000100007d010500000001418aa0e41d139d09b8f0000f048860757a4ce6aa660582867a"
      "8825b650c3abb8d2e053032a2f2a408df2b4a7b3c0ec90b22d5d8749ff839d29af4089f2b585ed6950958d279a18"
      "9e03f1ca5582265f59a75b0ac3111959c7e49004908db6e83f4096f2b16aee7f4b17cd65224b22d6765926a4a7b5"
      "2b528f840b60003f";
  testHttpInspectFound(header, Http::Utility::AlpnNames::get().Http2c);
}

TEST_F(HttpInspectorTest, InvalidConnectionPreface) {
  init();

  const std::string header = "505249202a20485454502f322e300d0a";
  std::vector<uint8_t> data = Hex::decode(std::string(header));
#ifdef WIN32
  EXPECT_CALL(os_sys_calls_, readv(_, _, _))
      .WillOnce(Invoke([&data](os_fd_t fd, const iovec* iov, int iovcnt) -> Api::SysCallSizeResult {
        ASSERT(iov->iov_len >= data.size());
        memcpy(iov->iov_base, data.data(), data.size());
        return Api::SysCallSizeResult{ssize_t(data.size()), 0};
      }))
      .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
#else
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
#endif
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
  file_event_callback_(Event::FileReadyType::Read);
  auto status = filter_->onData(*buffer_);
  EXPECT_EQ(status, Network::FilterStatus::StopIteration);
  EXPECT_EQ(0, cfg_->stats().http_not_found_.value());
}

TEST_F(HttpInspectorTest, MultipleReadsHttp2) {
  const std::string header =
      "505249202a20485454502f322e300d0a0d0a534d0d0a0d0a00000c04000000000000041000000000020000000000"
      "00040800000000000fff000100007d010500000001418aa0e41d139d09b8f0000f048860757a4ce6aa660582867a"
      "8825b650c3abb8d2e053032a2f2a408df2b4a7b3c0ec90b22d5d8749ff839d29af4089f2b585ed6950958d279a18"
      "9e03f1ca5582265f59a75b0ac3111959c7e49004908db6e83f4096f2b16aee7f4b17cd65224b22d6765926a4a7b5"
      "2b528f840b60003f";
  testHttpInspectMultipleReadsFound(header, Http::Utility::AlpnNames::get().Http2c);
}

TEST_F(HttpInspectorTest, MultipleReadsHttp2BadPreface) {
  const std::string header = "505249202a20485454502f322e300d0a0d0c";
  testHttpInspectMultipleReadsNotFound(header, true);
}

TEST_F(HttpInspectorTest, MultipleReadsHttp1) {
  const absl::string_view data = "GET /anything HTTP/1.0\r";
  testHttpInspectMultipleReadsFound(data, Http::Utility::AlpnNames::get().Http10);
}

TEST_F(HttpInspectorTest, MultipleReadsHttp1IncompleteBadHeader) {
  const absl::string_view data = "X";
  testHttpInspectMultipleReadsNotFound(data);
}

TEST_F(HttpInspectorTest, MultipleReadsHttp1BadProtocol) {
#ifdef ENVOY_ENABLE_UHV
  // permissive parsing
  return;
#endif

  const std::string valid_header = "GET /index HTTP/1.1\r";
  //  offset:                       0         10
  const std::string truncate_header = valid_header.substr(0, 14).append("\r");
  testHttpInspectMultipleReadsNotFound(truncate_header);
}

TEST_F(HttpInspectorTest, Http1WithLargeRequestLine) {
  // Verify that the http inspector can detect http requests
  // with large request line even when they are split over
  // multiple recv calls.
  init();
  absl::string_view method = "GET", http = "/index HTTP/1.0\r";
  std::string spaces(Config::MAX_INSPECT_SIZE - method.size() - http.size(), ' ');
  const std::string data = absl::StrCat(method, spaces, http);
  {
    InSequence s;
#ifdef WIN32
    EXPECT_CALL(os_sys_calls_, readv(_, _, _))
        .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
#else
    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
      return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
    }));
#endif

    uint64_t num_loops = Config::MAX_INSPECT_SIZE;
#if defined(__has_feature) &&                                                                      \
    ((__has_feature(thread_sanitizer)) || (__has_feature(address_sanitizer)))
    num_loops = 2;
#endif

#ifdef WIN32
    auto ctr = std::make_shared<size_t>(0);
    auto copy_len = std::make_shared<size_t>(1);
    EXPECT_CALL(os_sys_calls_, readv(_, _, _))
        .Times(num_loops)
        .WillRepeatedly(
            Invoke([&data, ctr, copy_len, num_loops](os_fd_t fd, const iovec* iov,
                                                     int iovcnt) -> Api::SysCallSizeResult {
              ASSERT(iov->iov_len >= 1);
              memcpy(iov->iov_base, data.data() + *ctr, 1);
              *ctr += 1;
              return Api::SysCallSizeResult{ssize_t(1), 0};
            }));
#else
    auto ctr = std::make_shared<size_t>(1);
    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
        .Times(num_loops)
        .WillRepeatedly(Invoke([&data, ctr, num_loops](os_fd_t, void* buffer, size_t length,
                                                       int) -> Api::SysCallSizeResult {
          size_t len = (*ctr);
          if (num_loops == 2) {
            ASSERT(*ctr != 3);
            len = size_t(Config::MAX_INSPECT_SIZE / (3 - (*ctr)));
          }
          ASSERT(length >= len);
          memcpy(buffer, data.data(), len);
          *ctr += 1;
          return Api::SysCallSizeResult{ssize_t(len), 0};
        }));
#endif
  }

  bool got_continue = false;
  const std::vector<absl::string_view> alpn_protos{Http::Utility::AlpnNames::get().Http10};
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
  while (!got_continue) {
    file_event_callback_(Event::FileReadyType::Read);
    auto status = filter_->onData(*buffer_);
    if (status == Network::FilterStatus::Continue) {
      got_continue = true;
    }
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
#ifdef WIN32
    EXPECT_CALL(os_sys_calls_, readv(_, _, _))
        .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
    for (size_t i = 0; i < 20; i++) {
      EXPECT_CALL(os_sys_calls_, readv(_, _, _))
          .WillOnce(Invoke(
              [&data, i](os_fd_t fd, const iovec* iov, int iovcnt) -> Api::SysCallSizeResult {
                ASSERT(iov->iov_len >= 20);
                memcpy(iov->iov_base, data.data() + i, 1);
                return Api::SysCallSizeResult{ssize_t(1), 0};
              }))
          .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
    }
#else
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
#endif
  }

  bool got_continue = false;
  const std::vector<absl::string_view> alpn_protos{Http::Utility::AlpnNames::get().Http10};
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
  while (!got_continue) {
    file_event_callback_(Event::FileReadyType::Read);
    auto status = filter_->onData(*buffer_);
    if (status == Network::FilterStatus::Continue) {
      got_continue = true;
    }
  }
  EXPECT_EQ(1, cfg_->stats().http10_found_.value());
}

} // namespace
} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
