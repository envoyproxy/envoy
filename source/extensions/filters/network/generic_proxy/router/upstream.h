#pragma once

#include <cstdint>

#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/generic_proxy/interface/codec.h"

#include "quiche/common/quiche_linked_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {

constexpr absl::string_view RouterFilterName = "envoy.filters.generic.router";
constexpr absl::string_view RouterFilterEncoderDecoderName = "envoy.filters.generic.router.codec";

class UpstreamRequestCallbacks : public Envoy::Event::DeferredDeletable {
public:
  // Following methods may remove itself from the upstream clean up the upstream
  // if necessary.

  virtual void onUpstreamFailure(ConnectionPool::PoolFailureReason reason,
                                 absl::string_view transport_failure_reason) PURE;
  virtual void onUpstreamSuccess() PURE;

  virtual void onConnectionClose(Network::ConnectionEvent event) PURE;
  virtual void onDecodingSuccess(ResponseHeaderFramePtr header_frame,
                                 absl::optional<StartTime> start_time = {}) PURE;
  virtual void onDecodingSuccess(ResponseCommonFramePtr common_frame) PURE;
  virtual void onDecodingFailure(absl::string_view reason) PURE;
};

class GenericUpstream {
public:
  virtual ~GenericUpstream() = default;

  // Insert a pending request that is waiting response into the upstream when the upstream
  // request is started.
  virtual void appendUpstreamRequest(uint64_t stream_id,
                                     UpstreamRequestCallbacks* pending_request) PURE;

  // Remove a pending request that is waiting response from the upstream when the upstream
  // request is reset or completed.
  virtual void removeUpstreamRequest(uint64_t stream_id) PURE;

  // Return the upstream host description.
  virtual Upstream::HostDescriptionConstSharedPtr upstreamHost() const PURE;

  // Client codec to encode the upstream request and decode the upstream response. This is
  // called when the upstream connection is ready.
  virtual ClientCodec& clientCodec() PURE;

  // Return the upstream connection. This could be called after the upstream connection is ready.
  virtual OptRef<Network::Connection> upstreamConnection() PURE;

  // Clean up the upstream. If close_connection is true, the connection will be closed.
  // Any implementation should ensure that it is safe to call cleanUp() multiple times and
  // ensure it works correctly.
  virtual void cleanUp(bool close_connection) PURE;
};

using GenericUpstreamSharedPtr = std::shared_ptr<GenericUpstream>;

class GenericUpstreamFactory {
public:
  virtual ~GenericUpstreamFactory() = default;

  virtual GenericUpstreamSharedPtr createGenericUpstream(Upstream::ThreadLocalCluster& cluster,
                                                         Upstream::LoadBalancerContext* context,
                                                         Network::Connection& downstream_conn,
                                                         const CodecFactory& codec_factory,
                                                         bool bound) const PURE;
};

template <class RequestManager>
class EncoderDecoder : public ClientCodecCallbacks, public StreamInfo::FilterState::Object {
public:
  EncoderDecoder(Network::Connection& connection, Upstream::HostDescriptionConstSharedPtr host,
                 ClientCodecPtr client_codec)
      : connection_(connection), host_(std::move(host)), client_codec_(std::move(client_codec)) {
    client_codec_->setCodecCallbacks(*this);
  }

  ClientCodec& clientCodec() { return *client_codec_; }

  // Insert a pending request that is waiting response into the encoder/decoder.
  void appendUpstreamRequest(uint64_t stream_id, UpstreamRequestCallbacks* pending_request) {
    request_manager_.appendUpstreamRequest(stream_id, pending_request);
  }

  // Remove a pending request that is waiting response from the encoder/decoder.
  void removeUpstreamRequest(uint64_t stream_id) {
    request_manager_.removeUpstreamRequest(stream_id);
  }

  // Called when the upstream connection is closed. All pending requests should
  // be failed.
  void onConnectionClose(Network::ConnectionEvent event) {
    request_manager_.onConnectionClose(event);
  }

  size_t requestsSize() const { return request_manager_.size(); }
  bool containsRequest(uint64_t stream_id) const { return request_manager_.contains(stream_id); }

  // ClientCodecCallbacks
  void onDecodingSuccess(ResponseHeaderFramePtr header_frame,
                         absl::optional<StartTime> start_time = {}) override {
    request_manager_.onDecodingSuccess(std::move(header_frame), std::move(start_time));
  }
  void onDecodingSuccess(ResponseCommonFramePtr common_frame) override {
    request_manager_.onDecodingSuccess(std::move(common_frame));
  }
  void onDecodingFailure(absl::string_view reason) override {
    request_manager_.onDecodingFailure(reason);
  }
  void writeToConnection(Buffer::Instance& buffer) override {
    if (connection_.state() == Network::Connection::State::Open) {
      connection_.write(buffer, false);
    }
  }
  OptRef<Network::Connection> connection() override {
    return connection_.state() == Network::Connection::State::Open
               ? OptRef<Network::Connection>{connection_}
               : OptRef<Network::Connection>{};
  }
  OptRef<const Upstream::ClusterInfo> upstreamCluster() const override { return host_->cluster(); }

private:
  Network::Connection& connection_;
  Upstream::HostDescriptionConstSharedPtr host_;
  ClientCodecPtr client_codec_;
  RequestManager request_manager_{};
};

class SharedRequestManager : Logger::Loggable<Logger::Id::upstream> {
public:
  void appendUpstreamRequest(uint64_t stream_id, UpstreamRequestCallbacks* pending_request);
  void removeUpstreamRequest(uint64_t stream_id);
  void onConnectionClose(Network::ConnectionEvent event);

  void onDecodingSuccess(ResponseHeaderFramePtr header_frame, absl::optional<StartTime> start_time);
  void onDecodingSuccess(ResponseCommonFramePtr common_frame);
  void onDecodingFailure(absl::string_view reason);

  size_t size() const { return pending_requests_.size(); }
  bool contains(uint64_t stream_id) const { return pending_requests_.contains(stream_id); }

  absl::flat_hash_map<uint64_t, UpstreamRequestCallbacks*> pending_requests_;
};

class UniqueRequestManager : Logger::Loggable<Logger::Id::upstream> {
public:
  void appendUpstreamRequest(uint64_t stream_id, UpstreamRequestCallbacks* pending_request);
  void removeUpstreamRequest(uint64_t stream_id);
  void onConnectionClose(Network::ConnectionEvent event);

  // ClientCodecCallbacks
  void onDecodingSuccess(ResponseHeaderFramePtr header_frame,
                         absl::optional<StartTime> start_time = {});
  void onDecodingSuccess(ResponseCommonFramePtr common_frame);
  void onDecodingFailure(absl::string_view reason);

  size_t size() const { return pending_request_ != nullptr ? 1 : 0; }
  // Stream id is not used in the unique request manager.
  bool contains(uint64_t) const { return pending_request_ != nullptr; }

  UpstreamRequestCallbacks* pending_request_{};
};

using SharedEncoderDecoder = EncoderDecoder<SharedRequestManager>;
using UniqueEncoderDecoder = EncoderDecoder<UniqueRequestManager>;
using SharedEncoderDecoderSharedPtr = std::shared_ptr<SharedEncoderDecoder>;
using UniqueEncoderDecoderSharedPtr = std::shared_ptr<UniqueEncoderDecoder>;

template <class EncoderDecoderType>
class UpstreamBase : public GenericUpstream,
                     public Tcp::ConnectionPool::Callbacks,
                     public Tcp::ConnectionPool::UpstreamCallbacks,
                     public Logger::Loggable<Logger::Id::upstream> {
public:
  UpstreamBase(Upstream::TcpPoolData pool_data, const CodecFactory& codec_factory)
      : tcp_pool_data_(std::move(pool_data)), codec_factory_(codec_factory) {}
  ~UpstreamBase() override {
    if (tcp_pool_handle_ != nullptr) {
      tcp_pool_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
    }
  }

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason, absl::string_view transport_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override {
    ENVOY_LOG(debug, "generic proxy upstream manager: on upstream connection failure (host: {})",
              host != nullptr ? host->address()->asStringView() : absl::string_view{});
    tcp_pool_handle_ = nullptr;

    onUpstreamFailure(reason, transport_reason);
  }
  void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Upstream::HostDescriptionConstSharedPtr host) override {
    ASSERT(host != nullptr);
    ENVOY_LOG(debug, "generic proxy upstream manager: on upstream connection ready (host: {})",
              host->address()->asStringView());
    tcp_pool_handle_ = nullptr;

    owned_conn_data_ = std::move(conn_data);
    encoder_decoder_ = getOrCreateEncoderDecoder(owned_conn_data_->connection());
    owned_conn_data_->addUpstreamCallbacks(*this);

    onUpstreamSuccess();
  }

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override {
    if (data.length() == 0) {
      return;
    }
    ASSERT(encoder_decoder_ != nullptr);
    encoder_decoder_->clientCodec().decode(data, end_stream);
  }

  // Upstream
  ClientCodec& clientCodec() override {
    ASSERT(encoder_decoder_ != nullptr);
    return encoder_decoder_->clientCodec();
  }
  void cleanUp(bool close_connection) override {
    ENVOY_LOG(debug, "generic proxy upstream manager: clean up upstream (close: {})",
              close_connection);

    if (tcp_pool_handle_ != nullptr) {
      ENVOY_LOG(debug, "generic proxy upstream manager: cancel pending connection");
      ASSERT(owned_conn_data_ == nullptr);

      // Clear the data first.
      auto local_tcp_pool_handle = tcp_pool_handle_;
      tcp_pool_handle_ = nullptr;
      local_tcp_pool_handle->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
    }

    if (owned_conn_data_ != nullptr) {
      if (!close_connection) {
        return;
      }

      ENVOY_LOG(debug, "generic proxy upstream request: close upstream connection");
      ASSERT(tcp_pool_handle_ == nullptr);

      // Clear the data first to avoid re-entering this function in the close callback.
      auto local_conn_data = std::move(owned_conn_data_);
      owned_conn_data_.reset();
      local_conn_data->connection().close(Network::ConnectionCloseType::FlushWrite);
    }
  }
  OptRef<Network::Connection> upstreamConnection() override {
    if (owned_conn_data_ == nullptr) {
      return {};
    }
    return owned_conn_data_->connection();
  }
  Upstream::HostDescriptionConstSharedPtr upstreamHost() const override {
    return tcp_pool_data_.host();
  }

  virtual void onUpstreamSuccess() PURE;
  virtual void onUpstreamFailure(ConnectionPool::PoolFailureReason reason,
                                 absl::string_view transport_failure_reason) PURE;

protected:
  void tryInitialize() {
    if (!initialized_) {
      initialized_ = true;
      tcp_pool_handle_ = tcp_pool_data_.newConnection(*this);
    }
  }
  EncoderDecoderType* getOrCreateEncoderDecoder(Network::Connection& connection) {
    EncoderDecoderType* encoder_decoder =
        connection.streamInfo().filterState()->getDataMutable<EncoderDecoderType>(
            RouterFilterEncoderDecoderName);
    if (encoder_decoder == nullptr) {
      auto data = std::make_unique<EncoderDecoderType>(connection, tcp_pool_data_.host(),
                                                       codec_factory_.createClientCodec());
      encoder_decoder = data.get();
      connection.streamInfo().filterState()->setData(RouterFilterEncoderDecoderName,
                                                     std::move(data),
                                                     StreamInfo::FilterState::StateType::Mutable,
                                                     StreamInfo::FilterState::LifeSpan::Connection);
    }
    return encoder_decoder;
  }

  Upstream::TcpPoolData tcp_pool_data_;
  Tcp::ConnectionPool::Cancellable* tcp_pool_handle_{};
  Tcp::ConnectionPool::ConnectionDataPtr owned_conn_data_;
  const CodecFactory& codec_factory_;
  EncoderDecoderType* encoder_decoder_{};

  // Whether the upstream connection is created. This will be set to true when
  // the initialize() is called.
  bool initialized_{};
};

struct EventWatcher : public Network::ConnectionCallbacks {
  using EventWatcherFn = std::function<void(Network::ConnectionEvent)>;
  EventWatcher(EventWatcherFn fn) : fn_(std::move(fn)) {}
  // Network::ConnectionCallbacks
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}
  void onEvent(Network::ConnectionEvent downstream_event) override { fn_(downstream_event); }
  EventWatcherFn fn_;
};

using BoundGenericUpstreamBase = UpstreamBase<SharedEncoderDecoder>;
class BoundGenericUpstream : public BoundGenericUpstreamBase,
                             public StreamInfo::FilterState::Object,
                             public std::enable_shared_from_this<BoundGenericUpstream> {
public:
  BoundGenericUpstream(Envoy::Upstream::TcpPoolData tcp_pool_data,
                       const CodecFactory& codec_factory,
                       Network::Connection& downstream_connection);

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onEvent(Network::ConnectionEvent event) override;

  // UpstreamBase
  void onUpstreamSuccess() override;
  void onUpstreamFailure(ConnectionPool::PoolFailureReason reason,
                         absl::string_view transport_failure_reason) override;

  // Upstream
  void appendUpstreamRequest(uint64_t stream_id,
                             UpstreamRequestCallbacks* pending_request) override;
  void removeUpstreamRequest(uint64_t stream_id) override;
  void cleanUp(bool close_connection) override;

  size_t waitingUpstreamRequestsSize() const { return pending_requests_.size(); }
  size_t waitingResponseRequestsSize() const {
    return encoder_decoder_ ? encoder_decoder_->requestsSize() : 0;
  }

private:
  void onDownstreamConnectionEvent(Network::ConnectionEvent event);

  Network::Connection& downstream_conn_;
  EventWatcher connection_event_watcher_;
  absl::optional<bool> upstream_conn_ok_;

  // This ensure the requests that are waiting upstream connection will be list in the order in
  // which the requests were received. By this way, the protocols that require the requests and
  // responses be handled in pipeline could works properly.
  // Note we need not do that for the requests that are waiting responses. Because we assume that
  // the clients of these protocols will send requests in order. Then when the connection is ready,
  // generic proxy will send requests to server in order. Finally, the upstream server of these
  // protocols will send responses in order. We also assume that the L7 filter chain of these
  // protocols will not change the processing order.
  using LinkedAbslHashMap = quiche::QuicheLinkedHashMap<uint64_t, UpstreamRequestCallbacks*>;
  LinkedAbslHashMap pending_requests_;
};

using OwnedGenericUpstreamBase = UpstreamBase<UniqueEncoderDecoder>;
class OwnedGenericUpstream : public OwnedGenericUpstreamBase {
public:
  using UpstreamBase::UpstreamBase;

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onEvent(Network::ConnectionEvent event) override;

  // UpstreamBase
  void onUpstreamSuccess() override;
  void onUpstreamFailure(ConnectionPool::PoolFailureReason reason,
                         absl::string_view transport_failure_reason) override;

  // Upstream
  void appendUpstreamRequest(uint64_t stream_id,
                             UpstreamRequestCallbacks* pending_request) override;
  void removeUpstreamRequest(uint64_t stream_id) override;

  size_t waitingUpstreamRequestsSize() const { return upstream_request_ ? 1 : 0; }
  size_t waitingResponseRequestsSize() const {
    return encoder_decoder_ ? encoder_decoder_->requestsSize() : 0;
  }

private:
  UpstreamRequestCallbacks* upstream_request_{};
};

class ProdGenericUpstreamFactory : public GenericUpstreamFactory {
public:
  GenericUpstreamSharedPtr createGenericUpstream(Upstream::ThreadLocalCluster& cluster,
                                                 Upstream::LoadBalancerContext* context,
                                                 Network::Connection& downstream_conn,
                                                 const CodecFactory& codec_factory,
                                                 bool bound) const override;
};

using DefaultGenericUpstreamFactory = ConstSingleton<ProdGenericUpstreamFactory>;

} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
