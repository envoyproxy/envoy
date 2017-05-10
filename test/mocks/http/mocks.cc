#include "mocks.h"

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Http {

MockConnectionManagerConfig::MockConnectionManagerConfig() {
  ON_CALL(*this, generateRequestId()).WillByDefault(Return(true));
}

MockConnectionManagerConfig::~MockConnectionManagerConfig() {}

MockConnectionCallbacks::MockConnectionCallbacks() {}
MockConnectionCallbacks::~MockConnectionCallbacks() {}

MockServerConnectionCallbacks::MockServerConnectionCallbacks() {}
MockServerConnectionCallbacks::~MockServerConnectionCallbacks() {}

MockStreamDecoder::MockStreamDecoder() {}
MockStreamDecoder::~MockStreamDecoder() {}

MockStreamCallbacks::MockStreamCallbacks() {}
MockStreamCallbacks::~MockStreamCallbacks() {}

MockStream::MockStream() {
  ON_CALL(*this, addCallbacks(_))
      .WillByDefault(
          Invoke([this](StreamCallbacks& callbacks) -> void { callbacks_.push_back(&callbacks); }));

  ON_CALL(*this, removeCallbacks(_))
      .WillByDefault(
          Invoke([this](StreamCallbacks& callbacks) -> void { callbacks_.remove(&callbacks); }));

  ON_CALL(*this, resetStream(_))
      .WillByDefault(Invoke([this](StreamResetReason reason) -> void {
        for (StreamCallbacks* callbacks : callbacks_) {
          callbacks->onResetStream(reason);
        }
      }));
}

MockStream::~MockStream() {}

MockStreamEncoder::MockStreamEncoder() {
  ON_CALL(*this, getStream()).WillByDefault(ReturnRef(stream_));
}

MockStreamEncoder::~MockStreamEncoder() {}

MockServerConnection::MockServerConnection() {
  ON_CALL(*this, protocol()).WillByDefault(Return(protocol_));
}

MockServerConnection::~MockServerConnection() {}

MockClientConnection::MockClientConnection() {}
MockClientConnection::~MockClientConnection() {}

MockFilterChainFactory::MockFilterChainFactory() {}
MockFilterChainFactory::~MockFilterChainFactory() {}

template <class T> static void initializeMockStreamFilterCallbacks(T& callbacks) {
  callbacks.route_.reset(new NiceMock<Router::MockRoute>());
  ON_CALL(callbacks, addResetStreamCallback(_))
      .WillByDefault(SaveArg<0>(&callbacks.reset_callback_));
  ON_CALL(callbacks, dispatcher()).WillByDefault(ReturnRef(callbacks.dispatcher_));
  ON_CALL(callbacks, requestInfo()).WillByDefault(ReturnRef(callbacks.request_info_));
  ON_CALL(callbacks, route()).WillByDefault(Return(callbacks.route_));
  ON_CALL(callbacks, downstreamAddress()).WillByDefault(ReturnRef(callbacks.downstream_address_));
}

MockStreamDecoderFilterCallbacks::MockStreamDecoderFilterCallbacks() {
  initializeMockStreamFilterCallbacks(*this);
  ON_CALL(*this, decodingBuffer()).WillByDefault(Return(buffer_.get()));
}

MockStreamDecoderFilterCallbacks::~MockStreamDecoderFilterCallbacks() {}

MockStreamEncoderFilterCallbacks::MockStreamEncoderFilterCallbacks() {
  initializeMockStreamFilterCallbacks(*this);
  ON_CALL(*this, encodingBuffer()).WillByDefault(Return(buffer_.get()));
}

MockStreamEncoderFilterCallbacks::~MockStreamEncoderFilterCallbacks() {}

MockStreamDecoderFilter::MockStreamDecoderFilter() {
  ON_CALL(*this, setDecoderFilterCallbacks(_))
      .WillByDefault(Invoke([this](StreamDecoderFilterCallbacks& callbacks) -> void {
        callbacks_ = &callbacks;
        callbacks_->addResetStreamCallback([this]() -> void { reset_stream_called_.ready(); });
      }));
}

MockStreamDecoderFilter::~MockStreamDecoderFilter() {}

MockStreamEncoderFilter::MockStreamEncoderFilter() {
  ON_CALL(*this, setEncoderFilterCallbacks(_))
      .WillByDefault(Invoke([this](StreamEncoderFilterCallbacks& callbacks)
                                -> void { callbacks_ = &callbacks; }));
}

MockStreamEncoderFilter::~MockStreamEncoderFilter() {}

MockAsyncClient::MockAsyncClient() {}
MockAsyncClient::~MockAsyncClient() {}

MockAsyncClientCallbacks::MockAsyncClientCallbacks() {}
MockAsyncClientCallbacks::~MockAsyncClientCallbacks() {}

MockAsyncClientStreamCallbacks::MockAsyncClientStreamCallbacks() {}
MockAsyncClientStreamCallbacks::~MockAsyncClientStreamCallbacks() {}

MockAsyncClientRequest::MockAsyncClientRequest(MockAsyncClient* client) : client_(client) {}
MockAsyncClientRequest::~MockAsyncClientRequest() { client_->onRequestDestroy(); }

MockFilterChainFactoryCallbacks::MockFilterChainFactoryCallbacks() {}
MockFilterChainFactoryCallbacks::~MockFilterChainFactoryCallbacks() {}

} // Http

namespace Http {
namespace ConnectionPool {

MockCancellable::MockCancellable() {}
MockCancellable::~MockCancellable() {}

MockInstance::MockInstance() {}
MockInstance::~MockInstance() {}

} // ConnectionPool
} // Http

namespace Http {
namespace AccessLog {

MockInstance::MockInstance() {}
MockInstance::~MockInstance() {}

MockRequestInfo::MockRequestInfo() {
  ON_CALL(*this, upstreamHost()).WillByDefault(Return(host_));
  ON_CALL(*this, startTime()).WillByDefault(Return(start_time_));
}

MockRequestInfo::~MockRequestInfo() {}

} // AccessLog
} // Http
} // Envoy
