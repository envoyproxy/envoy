#include "source/common/common/hex.h"
#include "source/common/http/utility.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/extensions/filters/listener/connect_handler/connect_handler.h"

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
namespace ConnectHandler {
namespace {

class ConnectHandlerTest : public testing::Test {
public:
  ConnectHandlerTest()
      : cfg_(std::make_shared<Config>(store_)),
        io_handle_(std::make_unique<Network::IoSocketHandleImpl>(42)) {}
  ~ConnectHandlerTest() override { io_handle_->close(); }

  void init(bool multiple_read = false) {
    filter_ = std::make_unique<Filter>(cfg_);
    EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
    EXPECT_CALL(socket_, detectedTransportProtocol()).WillRepeatedly(Return("raw_buffer"));
    EXPECT_CALL(cb_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(testing::Const(socket_), ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));
    EXPECT_CALL(socket_, ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));

    if (multiple_read) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));

      EXPECT_CALL(dispatcher_, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                                Event::FileReadyType::Read))
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

// Test skipping with a TLS request
TEST_F(ConnectHandlerTest, SkipConnectHandleForTLS) {
  filter_ = std::make_unique<Filter>(cfg_);

  EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
  EXPECT_CALL(socket_, ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));
  EXPECT_CALL(socket_, detectedTransportProtocol()).WillRepeatedly(Return("TLS"));
  EXPECT_EQ(filter_->onAccept(cb_), Network::FilterStatus::Continue);
}

// Test that filter detects peek errors.
TEST_F(ConnectHandlerTest, InlineReadIoError) {
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke([](os_fd_t, void*, size_t, int) -> Api::SysCallSizeResult {
        return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_NOT_SUP};
      }));
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, close());
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
  EXPECT_EQ(1, cfg_->stats().read_error_.value());
  // It's arguable if io error should bump the not_found counter
  EXPECT_EQ(0, cfg_->stats().connect_not_found_.value());
}

// Test that filter detects request which is not HTTP CONNECT.
TEST_F(ConnectHandlerTest, InlineReadNonConnect) {
  init();
  const absl::string_view data =
      "GET /anything HTTP/1.0\r\nhost: example.com\r\nuser-agent: "
      "curl/7.64.0\r\naccept: "
      "*/*\r\nx-forwarded-proto: http\r\nx-request-id: "
      "a52df4a0-ed00-4a19-86a7-80e5049c6c84\r\nx-envoy-expected-rq-timeout-ms: "
      "15000\r\ncontent-length: 0\r\n\r\n";
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));

  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::Continue);
  EXPECT_EQ(1, cfg_->stats().connect_not_found_.value());
}

// Test that filter detects closed remote connection in peeking data.
TEST_F(ConnectHandlerTest, InlineReadRemoteClose) {
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Return(Api::SysCallSizeResult{ssize_t(0), 0}));
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, close());
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
  EXPECT_EQ(0, cfg_->stats().connect_not_found_.value());
}

// Test that filter detects HTTP/0.9 Connect
TEST_F(ConnectHandlerTest, InlineReadHTTP09Connect) {
  init();
  const absl::string_view data = "CONNECT www.example.com:443 HTTP/0.9\r\n\r\n";

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, 0)).Times(0);
  EXPECT_CALL(os_sys_calls_, writev(42, _, _)).Times(0);
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::Continue);
  EXPECT_EQ(1, cfg_->stats().connect_not_found_.value());
}

// Test handling incoming HTTP/1.0 Connect request
TEST_F(ConnectHandlerTest, InlineReadHTTP10Connect) {
  init();
  const absl::string_view data = "CONNECT www.example.com:443 HTTP/1.0\r\n\r\n";
  const absl::string_view expect_response = "HTTP/1.0 200 Connection Established\r\n\r\n";

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, 0))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length == data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
  EXPECT_CALL(os_sys_calls_, writev(42, _, _))
      .WillOnce(Invoke(
          [&expect_response](os_fd_t, const iovec* iov, int num_iov) -> Api::SysCallSizeResult {
            ASSERT(num_iov == 1);
            ASSERT(absl::string_view(static_cast<char*>(iov[0].iov_base), iov[0].iov_len) ==
                   expect_response);
            return Api::SysCallSizeResult{ssize_t(expect_response.size()), 0};
          }));
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(socket_, setRequestedServerName("www.example.com"));
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::Continue);
  EXPECT_EQ(1, cfg_->stats().connect_found_.value());
}

// Test handling incoming HTTP/1.1 Connect request.
TEST_F(ConnectHandlerTest, InlineReadHTTP11Connect) {
  init();
  const absl::string_view data = "CONNECT www.example.com:443 HTTP/1.1\r\n\r\n";
  const absl::string_view expect_response = "HTTP/1.1 200 Connection Established\r\n\r\n";

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, 0))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length == data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
  EXPECT_CALL(os_sys_calls_, writev(42, _, _))
      .WillOnce(Invoke(
          [&expect_response](os_fd_t, const iovec* iov, int num_iov) -> Api::SysCallSizeResult {
            ASSERT(num_iov == 1);
            ASSERT(absl::string_view(static_cast<char*>(iov[0].iov_base), iov[0].iov_len) ==
                   expect_response);
            return Api::SysCallSizeResult{ssize_t(expect_response.size()), 0};
          }));
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(socket_, setRequestedServerName("www.example.com"));
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::Continue);
  EXPECT_EQ(1, cfg_->stats().connect_found_.value());
}

// Test that filter detects invalid Connect request.
TEST_F(ConnectHandlerTest, InlineReadParseError) {
  init();
  const absl::string_view data = "CONNECT \r\n\r\n";
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, 0)).Times(0);
  EXPECT_CALL(os_sys_calls_, writev(42, _, _)).Times(0);
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, close()).Times(0);
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::Continue);
  EXPECT_EQ(1, cfg_->stats().connect_not_found_.value());
}

// Test that filter detects read errors in draining data.
TEST_F(ConnectHandlerTest, DrainError) {
  init();
  const absl::string_view data = "CONNECT www.example.com:443 HTTP/1.1\r\n\r\n";
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, 0))
      .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_NOT_SUP}));
  EXPECT_CALL(os_sys_calls_, writev(42, _, _)).Times(0);
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, close());
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
  EXPECT_EQ(1, cfg_->stats().read_error_.value());
}

// Test that filter detects closed remote connection in draining data.
TEST_F(ConnectHandlerTest, DrainRemoteClose) {
  init();
  const absl::string_view data = "CONNECT www.example.com:443 HTTP/1.1\r\n\r\n";
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, 0))
      .WillOnce(Return(Api::SysCallSizeResult{ssize_t(0), 0}));
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, close());
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
  EXPECT_EQ(0, cfg_->stats().connect_not_found_.value());
}

// Test that filter detects write errors in sending response.
TEST_F(ConnectHandlerTest, ResponseError) {
  init();
  const absl::string_view data = "CONNECT www.example.com:443 HTTP/1.1\r\n\r\n";

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, 0))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length == data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
  EXPECT_CALL(os_sys_calls_, writev(42, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_NOT_SUP}));
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, close());
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
  EXPECT_EQ(1, cfg_->stats().write_error_.value());
}

// Test that filter detects closed remote connection in sending response.
TEST_F(ConnectHandlerTest, ResponseRemoteClose) {
  init();
  const absl::string_view data = "CONNECT www.example.com:443 HTTP/1.1\r\n\r\n";
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, 0))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length == data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
  EXPECT_CALL(os_sys_calls_, writev(42, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{ssize_t(0), 0}));
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, close());
  auto accepted = filter_->onAccept(cb_);
  EXPECT_EQ(accepted, Network::FilterStatus::StopIteration);
  EXPECT_EQ(0, cfg_->stats().connect_not_found_.value());
}

// Test with the Connect request spreads over multiple socket reads.
TEST_F(ConnectHandlerTest, MultipleRead) {
  init(true);
  const absl::string_view data = "CONNECT www.example.com:443 HTTP/1.1\r\n\r\n";
  const absl::string_view expect_response = "HTTP/1.1 200 Connection Established\r\n\r\n";
  {
    InSequence s;

    for (size_t i = 1; i <= data.size(); i++) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke(
              [&data, i](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= i);
                memcpy(buffer, data.data(), i);
                return Api::SysCallSizeResult{ssize_t(i), 0};
              }));
    }
    EXPECT_CALL(os_sys_calls_, recv(42, _, _, 0))
        .WillOnce(
            Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
              ASSERT(length == data.size());
              memcpy(buffer, data.data(), data.size());
              return Api::SysCallSizeResult{ssize_t(data.size()), 0};
            }));
    EXPECT_CALL(os_sys_calls_, writev(42, _, _))
        .WillOnce(Invoke(
            [&expect_response](os_fd_t, const iovec* iov, int num_iov) -> Api::SysCallSizeResult {
              ASSERT(num_iov == 1);
              ASSERT(absl::string_view(static_cast<char*>(iov[0].iov_base), iov[0].iov_len) ==
                     expect_response);
              return Api::SysCallSizeResult{ssize_t(expect_response.size()), 0};
            }));
  }

  bool got_continue = false;
  EXPECT_CALL(socket_, setRequestedServerName("www.example.com"));
  EXPECT_CALL(cb_, continueFilterChain(true)).WillOnce(InvokeWithoutArgs([&got_continue]() {
    got_continue = true;
  }));
  while (!got_continue) {
    file_event_callback_(Event::FileReadyType::Read);
  }
  EXPECT_EQ(1, cfg_->stats().connect_found_.value());
}

// Test Read error in multiple socket reads scenario.
TEST_F(ConnectHandlerTest, ErrorInMultipleRead) {
  init(true);
  const absl::string_view data = "CONNECT www.example.com:443";
  {
    InSequence s;

    for (size_t i = 1; i <= data.size(); i++) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke(
              [&data, i](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= i);
                memcpy(buffer, data.data(), i);
                return Api::SysCallSizeResult{ssize_t(i), 0};
              }));
    }
    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
        .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_NOT_SUP}));
  }

  bool got_continue = false;
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(false)).WillOnce(InvokeWithoutArgs([&got_continue]() {
    got_continue = true;
  }));
  while (!got_continue) {
    file_event_callback_(Event::FileReadyType::Read);
  }
  EXPECT_EQ(1, cfg_->stats().read_error_.value());
}
} // namespace
} // namespace ConnectHandler
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
