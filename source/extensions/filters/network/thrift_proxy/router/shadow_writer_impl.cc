#include "source/extensions/filters/network/thrift_proxy/router/shadow_writer_impl.h"

#include <memory>

#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/common/utility.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

OptRef<ShadowRouterHandle> ShadowWriterImpl::submit(const std::string& cluster_name,
                                                    MessageMetadataSharedPtr metadata,
                                                    TransportType original_transport,
                                                    ProtocolType original_protocol) {
  auto shadow_router = std::make_unique<ShadowRouterImpl>(*this, cluster_name, metadata,
                                                          original_transport, original_protocol);
  const bool created = shadow_router->createUpstreamRequest();
  if (!created || !tls_.get().has_value()) {
    stats_.routerStats().shadow_request_submit_failure_.inc();
    return absl::nullopt;
  }

  auto& active_routers = tls_->activeRouters();

  LinkedList::moveIntoList(std::move(shadow_router), active_routers);
  return *active_routers.front();
}

ShadowRouterImpl::ShadowRouterImpl(ShadowWriterImpl& parent, const std::string& cluster_name,
                                   MessageMetadataSharedPtr& metadata, TransportType transport_type,
                                   ProtocolType protocol_type)
    : RequestOwner(parent.clusterManager(), parent.stats()), parent_(parent),
      cluster_name_(cluster_name), metadata_(metadata->clone()), transport_type_(transport_type),
      protocol_type_(protocol_type),
      transport_(NamedTransportConfigFactory::getFactory(transport_type).createTransport()),
      protocol_(NamedProtocolConfigFactory::getFactory(protocol_type).createProtocol()) {
  response_decoder_ = std::make_unique<NullResponseDecoder>(*transport_, *protocol_);
  upstream_response_callbacks_ =
      std::make_unique<ShadowUpstreamResponseCallbacksImpl>(*response_decoder_);
}

Event::Dispatcher& ShadowRouterImpl::dispatcher() { return parent_.dispatcher(); }

bool ShadowRouterImpl::createUpstreamRequest() {
  auto prepare_result =
      prepareUpstreamRequest(cluster_name_, metadata_, transport_type_, protocol_type_, this);
  if (prepare_result.exception.has_value()) {
    return false;
  }

  auto& upstream_req_info = prepare_result.upstream_request_info.value();

  upstream_request_ = std::make_unique<UpstreamRequest>(*this, *upstream_req_info.conn_pool_data,
                                                        metadata_, upstream_req_info.transport,
                                                        upstream_req_info.protocol, true);
  upstream_request_->start();
  return true;
}

bool ShadowRouterImpl::requestStarted() const {
  return upstream_request_->conn_data_ != nullptr &&
         upstream_request_->upgrade_response_ == nullptr;
}

void ShadowRouterImpl::flushPendingCallbacks() {
  if (pending_callbacks_.empty()) {
    return;
  }

  for (auto& cb : pending_callbacks_) {
    cb();
  }

  pending_callbacks_.clear();
}

FilterStatus ShadowRouterImpl::runOrSave(std::function<FilterStatus()>&& cb,
                                         const std::function<void()>& on_save) {
  if (requestStarted()) {
    return cb();
  }

  pending_callbacks_.push_back(std::move(cb));

  if (on_save) {
    on_save();
  }

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::passthroughData(Buffer::Instance& data) {
  if (requestStarted()) {
    return ProtocolConverter::passthroughData(data);
  }

  auto copied = std::make_shared<Buffer::OwnedImpl>(data);
  auto cb = [copied = std::move(copied), this]() mutable -> FilterStatus {
    return ProtocolConverter::passthroughData(*copied);
  };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::structBegin(absl::string_view name) {
  if (requestStarted()) {
    return ProtocolConverter::structBegin(name);
  }

  auto cb = [name_str = std::string(name), this]() -> FilterStatus {
    return ProtocolConverter::structBegin(absl::string_view(name_str));
  };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::structEnd() {
  return runOrSave([this]() -> FilterStatus { return ProtocolConverter::structEnd(); });
}

FilterStatus ShadowRouterImpl::fieldBegin(absl::string_view name, FieldType& field_type,
                                          int16_t& field_id) {
  if (requestStarted()) {
    return ProtocolConverter::fieldBegin(name, field_type, field_id);
  }

  auto cb = [name_str = std::string(name), field_type, field_id, this]() mutable -> FilterStatus {
    return ProtocolConverter::fieldBegin(absl::string_view(name_str), field_type, field_id);
  };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::fieldEnd() {
  return runOrSave([this]() -> FilterStatus { return ProtocolConverter::fieldEnd(); });
}

FilterStatus ShadowRouterImpl::boolValue(bool& value) {
  return runOrSave(
      [value, this]() mutable -> FilterStatus { return ProtocolConverter::boolValue(value); });
}

FilterStatus ShadowRouterImpl::byteValue(uint8_t& value) {
  return runOrSave(
      [value, this]() mutable -> FilterStatus { return ProtocolConverter::byteValue(value); });
}

FilterStatus ShadowRouterImpl::int16Value(int16_t& value) {
  return runOrSave(
      [value, this]() mutable -> FilterStatus { return ProtocolConverter::int16Value(value); });
}

FilterStatus ShadowRouterImpl::int32Value(int32_t& value) {
  return runOrSave(
      [value, this]() mutable -> FilterStatus { return ProtocolConverter::int32Value(value); });
}

FilterStatus ShadowRouterImpl::int64Value(int64_t& value) {
  return runOrSave(
      [value, this]() mutable -> FilterStatus { return ProtocolConverter::int64Value(value); });
}

FilterStatus ShadowRouterImpl::doubleValue(double& value) {
  return runOrSave(
      [value, this]() mutable -> FilterStatus { return ProtocolConverter::doubleValue(value); });
}

FilterStatus ShadowRouterImpl::stringValue(absl::string_view value) {
  if (requestStarted()) {
    return ProtocolConverter::stringValue(value);
  }

  auto cb = [value_str = std::string(value), this]() -> FilterStatus {
    return ProtocolConverter::stringValue(absl::string_view(value_str));
  };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::mapBegin(FieldType& key_type, FieldType& value_type,
                                        uint32_t& size) {
  return runOrSave([key_type, value_type, size, this]() mutable -> FilterStatus {
    return ProtocolConverter::mapBegin(key_type, value_type, size);
  });
}

FilterStatus ShadowRouterImpl::mapEnd() {
  return runOrSave([this]() -> FilterStatus { return ProtocolConverter::mapEnd(); });
}

FilterStatus ShadowRouterImpl::listBegin(FieldType& elem_type, uint32_t& size) {
  return runOrSave([elem_type, size, this]() mutable -> FilterStatus {
    return ProtocolConverter::listBegin(elem_type, size);
  });
}

FilterStatus ShadowRouterImpl::listEnd() {
  return runOrSave([this]() -> FilterStatus { return ProtocolConverter::listEnd(); });
}

FilterStatus ShadowRouterImpl::setBegin(FieldType& elem_type, uint32_t& size) {
  return runOrSave([elem_type, size, this]() mutable -> FilterStatus {
    return ProtocolConverter::setBegin(elem_type, size);
  });
}

FilterStatus ShadowRouterImpl::setEnd() {
  return runOrSave([this]() -> FilterStatus { return ProtocolConverter::setEnd(); });
}

FilterStatus ShadowRouterImpl::messageEnd() {
  auto cb = [this]() -> FilterStatus {
    ASSERT(upstream_request_->conn_data_ != nullptr);

    ProtocolConverter::messageEnd();
    const auto encode_size = upstream_request_->encodeAndWrite(upstream_request_buffer_);
    addSize(encode_size);
    stats().recordUpstreamRequestSize(*cluster_, request_size_);

    request_sent_ = true;

    if (metadata_->messageType() == MessageType::Oneway) {
      upstream_request_->releaseConnection(false);
    }

    return FilterStatus::Continue;
  };

  return runOrSave(std::move(cb), [this]() -> void { request_ready_ = true; });
}

bool ShadowRouterImpl::requestInProgress() {
  const bool connection_open = upstream_request_->conn_data_ != nullptr;
  const bool connection_waiting = upstream_request_->conn_pool_handle_ != nullptr;

  // Connection open and message sent.
  const bool message_sent = connection_open && request_sent_;

  // Request ready to go and connection ready or almost ready.
  const bool message_ready = request_ready_ && (connection_open || connection_waiting);

  return message_sent || message_ready;
}

void ShadowRouterImpl::onRouterDestroy() {
  ASSERT(!deferred_deleting_);

  // Mark the shadow request to be destroyed when the response gets back
  // or the upstream connection finally fails.
  router_destroyed_ = true;

  if (!requestInProgress()) {
    maybeCleanup();
  }
}

bool ShadowRouterImpl::waitingForConnection() const {
  return upstream_request_->conn_pool_handle_ != nullptr;
}

void ShadowRouterImpl::maybeCleanup() {
  ENVOY_LOG(debug, "maybeCleanup, removed: {}, router_destroyed: {}", removed_, router_destroyed_);
  if (removed_) {
    return;
  }

  ASSERT(!deferred_deleting_);

  if (router_destroyed_) {
    removed_ = true;
    upstream_request_->resetStream();
    parent_.remove(*this);
  }
}

void ShadowRouterImpl::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  const bool done =
      upstream_request_->handleUpstreamData(data, end_stream, *upstream_response_callbacks_);
  if (done) {
    maybeCleanup();
  }
}

void ShadowRouterImpl::onEvent(Network::ConnectionEvent event) {
  upstream_request_->onEvent(event);
  maybeCleanup();
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
