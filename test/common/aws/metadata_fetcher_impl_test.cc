#include "common/aws/metadata_fetcher_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/test_common/simulated_time_system.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Aws {
namespace Auth {

class MetadataSessionTest : public testing::Test {
public:
  MetadataSessionTest()
      : connection_(new NiceMock<Network::MockClientConnection>()),
        codec_(new NiceMock<Http::MockClientConnection>()) {}

  std::unique_ptr<MetadataFetcherImpl::MetadataSession> createSession() {
    EXPECT_CALL(*connection_, addReadFilter(_));
    EXPECT_CALL(*connection_, addConnectionCallbacks(_));
    EXPECT_CALL(*connection_, connect());
    EXPECT_CALL(*connection_, noDelay(true));
    EXPECT_CALL(*codec_, newStream(_)).WillOnce(ReturnRef(encoder_));
    EXPECT_CALL(encoder_, getStream()).WillRepeatedly(ReturnRef(stream_));
    EXPECT_CALL(stream_, addCallbacks(Ref(decoder_)));
    EXPECT_CALL(encoder_, encodeHeaders(Ref(headers_), true));
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(InvokeWithoutArgs([]() {
      auto timer = new NiceMock<Event::MockTimer>();
      EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(5000)));
      return timer;
    }));
    return std::make_unique<MetadataFetcherImpl::MetadataSession>(
        Network::ClientConnectionPtr{connection_}, dispatcher_, decoder_, decoder_, headers_,
        [this](Network::Connection&, Http::ConnectionCallbacks&) { return codec_; });
  }

  NiceMock<Network::MockClientConnection>* connection_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Http::MockClientConnection>* codec_;
  MetadataFetcherImpl::StringBufferDecoder decoder_;
  Http::HeaderMapImpl headers_;
  NiceMock<Http::MockStreamEncoder> encoder_;
  NiceMock<Http::MockStream> stream_;
};

TEST_F(MetadataSessionTest, RemoteCloseConnected) {
  auto session = createSession();
  session->onEvent(Network::ConnectionEvent::Connected);
  EXPECT_CALL(*codec_, dispatch(_));
  EXPECT_CALL(stream_, resetStream(Http::StreamResetReason::ConnectionTermination));
  session->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(MetadataSessionTest, RemoteCloseNotConnected) {
  auto session = createSession();
  EXPECT_CALL(*codec_, dispatch(_));
  EXPECT_CALL(stream_, resetStream(Http::StreamResetReason::ConnectionFailure));
  session->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(MetadataSessionTest, LocalCloseConnected) {
  auto session = createSession();
  session->onEvent(Network::ConnectionEvent::Connected);
  EXPECT_CALL(stream_, resetStream(Http::StreamResetReason::ConnectionTermination));
  session->onEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(MetadataSessionTest, LocalCloseNotConnected) {
  auto session = createSession();
  EXPECT_CALL(stream_, resetStream(Http::StreamResetReason::ConnectionFailure));
  session->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(MetadataSessionTest, CloseNoFlush) {
  auto session = createSession();
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));
  session->close();
}

class MetadataFetcherImplTest : public testing::Test {
public:
  class MockStringBufferDecoder : public MetadataFetcherImpl::StringBufferDecoder {
  public:
    MOCK_CONST_METHOD0(body, const std::string&());
  };

  class MockMetadataSession : public MetadataFetcherImpl::MetadataSession {
  public:
    MOCK_METHOD0(close, void());
  };

  void expectConnection() {
    EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Invoke([](Network::Address::InstanceConstSharedPtr address,
                            Network::Address::InstanceConstSharedPtr, Network::TransportSocketPtr&,
                            const Network::ConnectionSocket::OptionsSharedPtr&) {
          EXPECT_EQ("127.0.0.1:80", address->asString());
          return nullptr;
        }));
  }

  void expectTimerRun(const std::chrono::milliseconds& delay) {
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this, delay](Event::TimerCb cb) {
      auto timer = new NiceMock<Event::MockTimer>();
      EXPECT_CALL(*timer, enableTimer(delay));
      expectConnection();
      cb();
      return timer;
    }));
    EXPECT_CALL(dispatcher_, exit());
    EXPECT_CALL(dispatcher_, run(Event::Dispatcher::RunType::Block));
  }

  MetadataFetcherPtr
  expectMetadata(const std::string& data,
                 const absl::optional<std::string>& auth_token = absl::optional<std::string>()) {
    return std::make_unique<MetadataFetcherImpl>(
        [&auth_token](Network::ClientConnectionPtr&&, Event::Dispatcher&, Http::StreamDecoder&,
                      Http::StreamCallbacks&, const Http::HeaderMap& headers,
                      MetadataFetcherImpl::HttpCodecFactory) {
          EXPECT_EQ(Http::Headers::get().MethodValues.Get,
                    headers.Method()->value().getStringView());
          EXPECT_EQ("127.0.0.1:80", headers.Host()->value().getStringView());
          EXPECT_EQ("/path", headers.Path()->value().getStringView());
          auto session = new NiceMock<MockMetadataSession>();
          EXPECT_CALL(*session, close());
          if (auth_token) {
            EXPECT_EQ(auth_token.value(), headers.Authorization()->value().getStringView());
          }
          return session;
        },
        [this, data]() {
          auto decoder = new NiceMock<MockStringBufferDecoder>();
          if (num_failures_ == 0) {
            expectTimerRun(std::chrono::milliseconds(0));
          } else {
            expectTimerRun(std::chrono::milliseconds(1000));
          }
          EXPECT_CALL(*decoder, body)
              .WillOnce(Invoke([this, &data, decoder]() -> const std::string& {
                Buffer::OwnedImpl buffer;
                if (num_failures_ >= required_failures_ && !data.empty()) {
                  buffer.add(data);
                } else {
                  num_failures_++;
                }
                decoder->decodeData(buffer, true);
                return decoder->body_;
              }));
          return decoder;
        });
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  int num_failures_{};
  int required_failures_{};
};

TEST_F(MetadataFetcherImplTest, SuccessfulRequest) {
  const auto fetcher = expectMetadata("test");
  const auto data = fetcher->getMetadata(dispatcher_, "127.0.0.1", "/path");
  EXPECT_EQ("test", data);
  EXPECT_EQ(0, num_failures_);
}

TEST_F(MetadataFetcherImplTest, SuccessfulAfter2ndRequest) {
  required_failures_ = 1;
  const auto fetcher = expectMetadata("test");
  const auto data = fetcher->getMetadata(dispatcher_, "127.0.0.1", "/path");
  EXPECT_EQ("test", data.value());
  EXPECT_EQ(1, num_failures_);
}

TEST_F(MetadataFetcherImplTest, SuccessfulAfter3rdRequest) {
  required_failures_ = 2;
  const auto fetcher = expectMetadata("test");
  const auto data = fetcher->getMetadata(dispatcher_, "127.0.0.1", "/path");
  EXPECT_EQ("test", data.value());
  EXPECT_EQ(2, num_failures_);
}

TEST_F(MetadataFetcherImplTest, AuthToken) {
  const auto fetcher = expectMetadata("test", "auth_token");
  const auto data = fetcher->getMetadata(dispatcher_, "127.0.0.1", "/path", "auth_token");
  EXPECT_EQ("test", data);
  EXPECT_EQ(0, num_failures_);
}

TEST_F(MetadataFetcherImplTest, FailureEveryRetry) {
  const auto fetcher = expectMetadata("");
  const auto data = fetcher->getMetadata(dispatcher_, "127.0.0.1", "/path");
  EXPECT_FALSE(data.has_value());
  EXPECT_EQ(4, num_failures_);
}

} // namespace Auth
} // namespace Aws
} // namespace Envoy