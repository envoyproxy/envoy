#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/router/router.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "source/extensions/filters/network/thrift_proxy/conn_manager.h"
#include "source/extensions/filters/network/thrift_proxy/router/router.h"
#include "source/extensions/filters/network/thrift_proxy/router/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

struct NullResponseDecoder : public DecoderCallbacks, public ProtocolConverter {
  NullResponseDecoder(Transport& transport, Protocol& protocol)
      : decoder_(std::make_unique<Decoder>(transport, protocol, *this)) {
    initProtocolConverter(protocol, response_buffer_);
  }

  virtual ThriftFilters::ResponseStatus upstreamData(Buffer::Instance& data) {
    upstream_buffer_.move(data);

    bool underflow = false;
    try {
      underflow = onData();
    } catch (const AppException&) {
      return ThriftFilters::ResponseStatus::Reset;
    } catch (const EnvoyException&) {
      return ThriftFilters::ResponseStatus::Reset;
    }

    ASSERT(complete_ || underflow);
    return complete_ ? ThriftFilters::ResponseStatus::Complete
                     : ThriftFilters::ResponseStatus::MoreData;
  }
  virtual bool onData() {
    bool underflow = false;
    decoder_->onData(upstream_buffer_, underflow);
    return underflow;
  }
  MessageMetadataSharedPtr& responseMetadata() {
    ASSERT(metadata_ != nullptr);
    return metadata_;
  }
  bool responseSuccess() { return success_.value_or(false); }

  // ProtocolConverter
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override {
    metadata_ = metadata;
    if (metadata_->hasReplyType()) {
      success_ = metadata_->replyType() == ReplyType::Success;
    }
    return FilterStatus::Continue;
  }
  FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override {
    UNREFERENCED_PARAMETER(metadata);
    return FilterStatus::Continue;
  }
  FilterStatus transportEnd() override {
    ASSERT(metadata_ != nullptr);
    complete_ = true;
    return FilterStatus::Continue;
  }

  // DecoderCallbacks
  DecoderEventHandler& newDecoderEventHandler() override { return *this; }
  bool passthroughEnabled() const override { return true; }
  bool isRequest() const override { return false; }
  bool headerKeysPreserveCase() const override { return false; }

  DecoderPtr decoder_;
  Buffer::OwnedImpl response_buffer_;
  Buffer::OwnedImpl upstream_buffer_;
  MessageMetadataSharedPtr metadata_;
  absl::optional<bool> success_;
  bool complete_ : 1;
};
using NullResponseDecoderPtr = std::unique_ptr<NullResponseDecoder>;

// Adapter from NullResponseDecoder to UpstreamResponseCallbacks.
class ShadowUpstreamResponseCallbacksImpl : public UpstreamResponseCallbacks {
public:
  ShadowUpstreamResponseCallbacksImpl(NullResponseDecoder& response_decoder)
      : response_decoder_(response_decoder) {}

  void startUpstreamResponse(Transport&, Protocol&) override {}
  ThriftFilters::ResponseStatus upstreamData(Buffer::Instance& buffer) override {
    return response_decoder_.upstreamData(buffer);
  }
  MessageMetadataSharedPtr responseMetadata() override {
    return response_decoder_.responseMetadata();
  }
  bool responseSuccess() override { return response_decoder_.responseSuccess(); }

private:
  NullResponseDecoder& response_decoder_;
};
using ShadowUpstreamResponseCallbacksImplPtr = std::unique_ptr<ShadowUpstreamResponseCallbacksImpl>;

class ShadowWriterImpl;

class ShadowRouterImpl : public ShadowRouterHandle,
                         public RequestOwner,
                         public Tcp::ConnectionPool::UpstreamCallbacks,
                         public Upstream::LoadBalancerContextBase,
                         public Event::DeferredDeletable,
                         public LinkedObject<ShadowRouterImpl> {
public:
  ShadowRouterImpl(ShadowWriterImpl& parent, const std::string& cluster_name,
                   MessageMetadataSharedPtr& metadata, TransportType transport_type,
                   ProtocolType protocol_type);
  ~ShadowRouterImpl() override = default;

  bool createUpstreamRequest();
  void maybeCleanup();
  void resetStream() {
    if (upstream_request_ != nullptr) {
      upstream_request_->releaseConnection(true);
    }
  }

  // ShadowRouterHandle
  void onRouterDestroy() override;
  bool waitingForConnection() const override;
  RequestOwner& requestOwner() override { return *this; }

  // RequestOwner
  Tcp::ConnectionPool::UpstreamCallbacks& upstreamCallbacks() override { return *this; }
  Buffer::OwnedImpl& buffer() override { return upstream_request_buffer_; }
  Event::Dispatcher& dispatcher() override;
  void addSize(uint64_t size) override { request_size_ += size; }
  void continueDecoding() override { flushPendingCallbacks(); }
  void resetDownstreamConnection() override {}
  void sendLocalReply(const ThriftProxy::DirectResponse&, bool) override {}

  // RequestOwner::ProtocolConverter
  FilterStatus transportBegin(MessageMetadataSharedPtr) override { return FilterStatus::Continue; }
  FilterStatus transportEnd() override { return FilterStatus::Continue; }
  FilterStatus messageEnd() override;
  FilterStatus passthroughData(Buffer::Instance& data) override;
  FilterStatus structBegin(absl::string_view name) override;
  FilterStatus structEnd() override;
  FilterStatus fieldBegin(absl::string_view name, FieldType& field_type,
                          int16_t& field_id) override;
  FilterStatus fieldEnd() override;
  FilterStatus boolValue(bool& value) override;
  FilterStatus byteValue(uint8_t& value) override;
  FilterStatus int16Value(int16_t& value) override;
  FilterStatus int32Value(int32_t& value) override;
  FilterStatus int64Value(int64_t& value) override;
  FilterStatus doubleValue(double& value) override;
  FilterStatus stringValue(absl::string_view value) override;
  FilterStatus mapBegin(FieldType& key_type, FieldType& value_type, uint32_t& size) override;
  FilterStatus mapEnd() override;
  FilterStatus listBegin(FieldType& elem_type, uint32_t& size) override;
  FilterStatus listEnd() override;
  FilterStatus setBegin(FieldType& elem_type, uint32_t& size) override;
  FilterStatus setEnd() override;

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // Upstream::LoadBalancerContextBase
  const Network::Connection* downstreamConnection() const override { return nullptr; }
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() override { return nullptr; }

  // Event::DeferredDeletable
  void deleteIsPending() override { deferred_deleting_ = true; }

private:
  friend class ShadowWriterTest;
  using ConverterCallback = std::function<FilterStatus()>;

  void writeRequest();
  bool requestInProgress();
  bool requestStarted() const;
  void flushPendingCallbacks();
  FilterStatus runOrSave(std::function<FilterStatus()>&& cb,
                         const std::function<void()>& on_save = {});

  ShadowWriterImpl& parent_;
  const std::string cluster_name_;
  MessageMetadataSharedPtr metadata_;
  const TransportType transport_type_;
  const ProtocolType protocol_type_;
  TransportPtr transport_;
  ProtocolPtr protocol_;
  NullResponseDecoderPtr response_decoder_;
  ShadowUpstreamResponseCallbacksImplPtr upstream_response_callbacks_;
  bool router_destroyed_{};
  bool request_sent_{};
  Buffer::OwnedImpl upstream_request_buffer_;
  std::unique_ptr<UpstreamRequest> upstream_request_;
  uint64_t request_size_{};
  uint64_t response_size_{};
  bool request_ready_ : 1;

  std::list<ConverterCallback> pending_callbacks_;
  bool removed_{};
  bool deferred_deleting_{};
};

class ActiveRouters : public ThreadLocal::ThreadLocalObject {
public:
  ActiveRouters(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}
  ~ActiveRouters() override {
    while (!active_routers_.empty()) {
      auto& router = active_routers_.front();
      router->resetStream();
      remove(*router);
    }
  }

  std::list<std::unique_ptr<ShadowRouterImpl>>& activeRouters() { return active_routers_; }

  void remove(ShadowRouterImpl& router) {
    dispatcher_.deferredDelete(router.removeFromList(active_routers_));
  }

private:
  Event::Dispatcher& dispatcher_;
  std::list<std::unique_ptr<ShadowRouterImpl>> active_routers_;
};

class ShadowWriterImpl : public ShadowWriter, Logger::Loggable<Logger::Id::thrift> {
public:
  ShadowWriterImpl(Upstream::ClusterManager& cm, const RouterStats& stats,
                   Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls)
      : cm_(cm), stats_(stats), dispatcher_(dispatcher), tls_(tls.allocateSlot()) {
    tls_->set([](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return std::make_shared<ActiveRouters>(dispatcher);
    });
  }

  ~ShadowWriterImpl() override = default;

  void remove(ShadowRouterImpl& router) { tls_->getTyped<ActiveRouters>().remove(router); }
  const RouterStats& stats() { return stats_; }

  // Router::ShadowWriter
  Upstream::ClusterManager& clusterManager() override { return cm_; }
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  absl::optional<std::reference_wrapper<ShadowRouterHandle>>
  submit(const std::string& cluster_name, MessageMetadataSharedPtr metadata,
         TransportType original_transport, ProtocolType original_protocol) override;

private:
  friend class ShadowRouterImpl;

  Upstream::ClusterManager& cm_;
  const RouterStats& stats_;
  Event::Dispatcher& dispatcher_;
  ThreadLocal::SlotPtr tls_;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
