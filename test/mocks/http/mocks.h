#pragma once

#include "envoy/http/access_log.h"
#include "envoy/http/async_client.h"
#include "envoy/http/codec.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/filter.h"

#include "common/http/conn_manager_impl.h"
#include "common/http/header_map_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/upstream/host.h"

namespace Http {
namespace AccessLog {

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  // Http::AccessLog::Instance
  MOCK_METHOD3(log, void(const Http::HeaderMap* request_headers,
                         const Http::HeaderMap* response_headers, const RequestInfo& request_info));
};

class MockRequestInfo : public RequestInfo {
public:
  MockRequestInfo();
  ~MockRequestInfo();

  // Http::AccessLog::RequestInfo
  MOCK_METHOD1(setResponseFlag, void(ResponseFlag response_flag));
  MOCK_METHOD1(onUpstreamHostSelected, void(Upstream::HostDescriptionPtr host));
  MOCK_CONST_METHOD0(startTime, SystemTime());
  MOCK_CONST_METHOD0(bytesReceived, uint64_t());
  MOCK_CONST_METHOD0(protocol, Protocol());
  MOCK_METHOD1(protocol, void(Protocol protocol));
  MOCK_CONST_METHOD0(responseCode, Optional<uint32_t>&());
  MOCK_CONST_METHOD0(bytesSent, uint64_t());
  MOCK_CONST_METHOD0(duration, std::chrono::milliseconds());
  MOCK_CONST_METHOD1(getResponseFlag, bool(Http::AccessLog::ResponseFlag));
  MOCK_CONST_METHOD0(upstreamHost, Upstream::HostDescriptionPtr());
  MOCK_CONST_METHOD0(healthCheck, bool());
  MOCK_METHOD1(healthCheck, void(bool is_hc));
};

} // AccessLog

class MockConnectionManagerConfig : public ConnectionManagerConfig {
public:
  MockConnectionManagerConfig();
  ~MockConnectionManagerConfig();

  // Http::ConnectionManagerConfig
  ServerConnectionPtr createCodec(Network::Connection& connection, const Buffer::Instance& instance,
                                  ServerConnectionCallbacks& callbacks) override {
    return ServerConnectionPtr{createCodec_(connection, instance, callbacks)};
  }

  MOCK_METHOD0(accessLogs, const std::list<AccessLog::InstancePtr>&());
  MOCK_METHOD3(createCodec_, ServerConnection*(Network::Connection&, const Buffer::Instance&,
                                               ServerConnectionCallbacks&));
  MOCK_METHOD0(dateProvider, DateProvider&());
  MOCK_METHOD0(drainTimeout, std::chrono::milliseconds());
  MOCK_METHOD0(filterFactory, FilterChainFactory&());
  MOCK_METHOD0(generateRequestId, bool());
  MOCK_METHOD0(idleTimeout, const Optional<std::chrono::milliseconds>&());
  MOCK_METHOD0(routeConfig, Router::Config&());
  MOCK_METHOD0(serverName, const std::string&());
  MOCK_METHOD0(stats, ConnectionManagerStats&());
  MOCK_METHOD0(tracingStats, ConnectionManagerTracingStats&());
  MOCK_METHOD0(useRemoteAddress, bool());
  MOCK_METHOD0(localAddress, const std::string&());
  MOCK_METHOD0(userAgent, const Optional<std::string>&());
  MOCK_METHOD0(tracingConfig, const Optional<Http::TracingConnectionManagerConfig>&());

  testing::NiceMock<Router::MockConfig> route_config_;
};

class MockConnectionCallbacks : public virtual ConnectionCallbacks {
public:
  MockConnectionCallbacks();
  ~MockConnectionCallbacks();

  // Http::ConnectionCallbacks
  MOCK_METHOD0(onGoAway, void());
};

class MockServerConnectionCallbacks : public ServerConnectionCallbacks,
                                      public MockConnectionCallbacks {
public:
  MockServerConnectionCallbacks();
  ~MockServerConnectionCallbacks();

  // Http::ServerConnectionCallbacks
  MOCK_METHOD1(newStream, StreamDecoder&(StreamEncoder& response_encoder));
};

class MockStreamDecoder : public StreamDecoder {
public:
  MockStreamDecoder();
  ~MockStreamDecoder();

  void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override {
    decodeHeaders_(headers, end_stream);
  }
  void decodeTrailers(HeaderMapPtr&& trailers) override { decodeTrailers_(trailers); }

  // Http::StreamDecoder
  MOCK_METHOD2(decodeHeaders_, void(HeaderMapPtr& headers, bool end_stream));
  MOCK_METHOD2(decodeData, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(decodeTrailers_, void(HeaderMapPtr& trailers));
};

class MockStreamCallbacks : public StreamCallbacks {
public:
  MockStreamCallbacks();
  ~MockStreamCallbacks();

  // Http::StreamCallbacks
  MOCK_METHOD1(onResetStream, void(StreamResetReason reason));
};

class MockStream : public Stream {
public:
  MockStream();
  ~MockStream();

  // Http::Stream
  MOCK_METHOD1(addCallbacks, void(StreamCallbacks& callbacks));
  MOCK_METHOD1(removeCallbacks, void(StreamCallbacks& callbacks));
  MOCK_METHOD1(resetStream, void(StreamResetReason reason));

  std::list<StreamCallbacks*> callbacks_{};
};

class MockStreamEncoder : public StreamEncoder {
public:
  MockStreamEncoder();
  ~MockStreamEncoder();

  // Http::StreamEncoder
  MOCK_METHOD2(encodeHeaders, void(const HeaderMap& headers, bool end_stream));
  MOCK_METHOD2(encodeData, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(encodeTrailers, void(const HeaderMap& trailers));
  MOCK_METHOD0(getStream, Stream&());

  testing::NiceMock<MockStream> stream_;
};

class MockServerConnection : public ServerConnection {
public:
  MockServerConnection();
  ~MockServerConnection();

  // Http::Connection
  MOCK_METHOD1(dispatch, void(Buffer::Instance& data));
  MOCK_METHOD0(goAway, void());
  MOCK_METHOD0(protocol, Protocol());
  MOCK_METHOD0(shutdownNotice, void());
  MOCK_METHOD0(wantsToWrite, bool());

  Protocol protocol_{Protocol::Http11};
};

class MockClientConnection : public ClientConnection {
public:
  MockClientConnection();
  ~MockClientConnection();

  // Http::Connection
  MOCK_METHOD1(dispatch, void(Buffer::Instance& data));
  MOCK_METHOD0(goAway, void());
  MOCK_METHOD0(protocol, Protocol());
  MOCK_METHOD0(shutdownNotice, void());
  MOCK_METHOD0(wantsToWrite, bool());

  // Http::ClientConnection
  MOCK_METHOD1(newStream, StreamEncoder&(StreamDecoder& response_decoder));
};

class MockFilterChainFactory : public FilterChainFactory {
public:
  MockFilterChainFactory();
  ~MockFilterChainFactory();

  // Http::FilterChainFactory
  MOCK_METHOD1(createFilterChain, void(FilterChainFactoryCallbacks& callbacks));
};

class MockStreamFilterCallbacksBase {
public:
  std::function<void()> reset_callback_;
  Event::MockDispatcher dispatcher_;
  testing::NiceMock<AccessLog::MockRequestInfo> request_info_;
  testing::NiceMock<Router::MockStableRouteTable> route_table_;
  std::string downstream_address_;
};

class MockStreamDecoderFilterCallbacks : public StreamDecoderFilterCallbacks,
                                         public MockStreamFilterCallbacksBase {
public:
  MockStreamDecoderFilterCallbacks();
  ~MockStreamDecoderFilterCallbacks();

  // Http::StreamFilterCallbacks
  MOCK_METHOD1(addResetStreamCallback, void(std::function<void()> callback));
  MOCK_METHOD0(connectionId, uint64_t());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(resetStream, void());
  MOCK_METHOD0(routeTable, Router::StableRouteTable&());
  MOCK_METHOD0(streamId, uint64_t());
  MOCK_METHOD0(requestInfo, Http::AccessLog::RequestInfo&());
  MOCK_METHOD0(downstreamAddress, const std::string&());

  // Http::StreamDecoderFilterCallbacks
  void encodeHeaders(HeaderMapPtr&& headers, bool end_stream) override {
    encodeHeaders_(*headers, end_stream);
  }
  void encodeTrailers(HeaderMapPtr&& trailers) override { encodeTrailers_(*trailers); }

  MOCK_METHOD0(continueDecoding, void());
  MOCK_METHOD0(decodingBuffer, const Buffer::Instance*());
  MOCK_METHOD2(encodeHeaders_, void(HeaderMap& headers, bool end_stream));
  MOCK_METHOD2(encodeData, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(encodeTrailers_, void(HeaderMap& trailers));
};

class MockStreamEncoderFilterCallbacks : public StreamEncoderFilterCallbacks,
                                         public MockStreamFilterCallbacksBase {
public:
  MockStreamEncoderFilterCallbacks();
  ~MockStreamEncoderFilterCallbacks();

  // Http::StreamFilterCallbacks
  MOCK_METHOD1(addResetStreamCallback, void(std::function<void()> callback));
  MOCK_METHOD0(connectionId, uint64_t());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(resetStream, void());
  MOCK_METHOD0(routeTable, Router::StableRouteTable&());
  MOCK_METHOD0(streamId, uint64_t());
  MOCK_METHOD0(requestInfo, Http::AccessLog::RequestInfo&());
  MOCK_METHOD0(downstreamAddress, const std::string&());

  // Http::StreamEncoderFilterCallbacks
  MOCK_METHOD0(continueEncoding, void());
  MOCK_METHOD0(encodingBuffer, const Buffer::Instance*());
};

class MockStreamDecoderFilter : public StreamDecoderFilter {
public:
  MockStreamDecoderFilter();
  ~MockStreamDecoderFilter();

  // Http::StreamDecoderFilter
  MOCK_METHOD2(decodeHeaders, FilterHeadersStatus(HeaderMap& headers, bool end_stream));
  MOCK_METHOD2(decodeData, FilterDataStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(decodeTrailers, FilterTrailersStatus(HeaderMap& trailers));
  MOCK_METHOD1(setDecoderFilterCallbacks, void(StreamDecoderFilterCallbacks& callbacks));

  Http::StreamDecoderFilterCallbacks* callbacks_{};
  ReadyWatcher reset_stream_called_;
};

class MockStreamEncoderFilter : public StreamEncoderFilter {
public:
  MockStreamEncoderFilter();
  ~MockStreamEncoderFilter();

  // Http::MockStreamEncoderFilter
  MOCK_METHOD2(encodeHeaders, FilterHeadersStatus(HeaderMap& headers, bool end_stream));
  MOCK_METHOD2(encodeData, FilterDataStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(encodeTrailers, FilterTrailersStatus(HeaderMap& trailers));
  MOCK_METHOD1(setEncoderFilterCallbacks, void(StreamEncoderFilterCallbacks& callbacks));

  Http::StreamEncoderFilterCallbacks* callbacks_{};
};

class MockAsyncClient : public AsyncClient {
public:
  MockAsyncClient();
  ~MockAsyncClient();

  MOCK_METHOD0(onRequestDestroy, void());

  // Http::AsyncClient
  Request* send(MessagePtr&& request, Callbacks& callbacks,
                const Optional<std::chrono::milliseconds>& timeout) override {
    return send_(request, callbacks, timeout);
  }

  MOCK_METHOD3(send_, Request*(MessagePtr& request, Callbacks& callbacks,
                               const Optional<std::chrono::milliseconds>& timeout));
};

class MockAsyncClientCallbacks : public AsyncClient::Callbacks {
public:
  MockAsyncClientCallbacks();
  ~MockAsyncClientCallbacks();

  void onSuccess(MessagePtr&& response) override { onSuccess_(response.get()); }

  // Http::AsyncClient::Callbacks
  MOCK_METHOD1(onSuccess_, void(Message* response));
  MOCK_METHOD1(onFailure, void(Http::AsyncClient::FailureReason reason));
};

class MockAsyncClientRequest : public AsyncClient::Request {
public:
  MockAsyncClientRequest(MockAsyncClient* client);
  ~MockAsyncClientRequest();

  MOCK_METHOD0(cancel, void());

  MockAsyncClient* client_;
};
} // Http

namespace Http {
namespace ConnectionPool {

class MockCancellable : public Cancellable {
public:
  MockCancellable();
  ~MockCancellable();

  // Http::ConnectionPool::Cancellable
  MOCK_METHOD0(cancel, void());
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  // Http::ConnectionPool::Instance
  MOCK_METHOD1(addDrainedCallback, void(DrainedCb cb));
  MOCK_METHOD2(newStream, Cancellable*(Http::StreamDecoder& response_decoder,
                                       Http::ConnectionPool::Callbacks& callbacks));

  std::shared_ptr<testing::NiceMock<Upstream::MockHostDescription>> host_{
      new testing::NiceMock<Upstream::MockHostDescription>()};
};

} // ConnectionPool
} // Http

MATCHER_P(HeaderMapEqual, rhs, "") {
  Http::HeaderMapImpl& lhs = *dynamic_cast<Http::HeaderMapImpl*>(arg.get());
  return lhs == *rhs;
}

MATCHER_P(HeaderMapEqualRef, rhs, "") {
  const Http::HeaderMapImpl& lhs = *dynamic_cast<const Http::HeaderMapImpl*>(&arg);
  return lhs == *dynamic_cast<const Http::HeaderMapImpl*>(rhs);
}
