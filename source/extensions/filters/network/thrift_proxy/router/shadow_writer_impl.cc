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

absl::optional<std::reference_wrapper<ShadowRouterHandle>>
ShadowWriterImpl::submit(const std::string& cluster_name, MessageMetadataSharedPtr metadata,
                         TransportType original_transport, ProtocolType original_protocol) {
  auto shadow_router = std::make_unique<ShadowRouterImpl>(*this, cluster_name, metadata,
                                                          original_transport, original_protocol);
  const bool created = shadow_router->createUpstreamRequest();
  if (!created) {
    return absl::nullopt;
  }

  LinkedList::moveIntoList(std::move(shadow_router), active_routers_);
  return *active_routers_.front();
}

ShadowRouterImpl::ShadowRouterImpl(ShadowWriterImpl& parent, const std::string& cluster_name,
                                   MessageMetadataSharedPtr& metadata, TransportType transport_type,
                                   ProtocolType protocol_type)
    : RequestOwner(parent.clusterManager(), parent.statPrefix(), parent.scope()), parent_(parent),
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

  upstream_request_ =
      std::make_unique<UpstreamRequest>(*this, *upstream_req_info.conn_pool_data, metadata_,
                                        upstream_req_info.transport, upstream_req_info.protocol);
  upstream_request_->start();
  return true;
}

bool ShadowRouterImpl::requestStarted() const {
  return upstream_request_->conn_data_ != nullptr &&
         upstream_request_->upgrade_response_ == nullptr;
}

FilterStatus ShadowRouterImpl::passthroughData(Buffer::Instance& data) {
  if (requestStarted()) {
    return ProtocolConverter::passthroughData(data);
  }

  auto copied = std::make_shared<Buffer::OwnedImpl>(data);
  auto cb = [copied = std::move(copied), this]() mutable {
    ProtocolConverter::passthroughData(*copied);
  };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::structBegin(absl::string_view name) {
  if (requestStarted()) {
    return ProtocolConverter::structBegin(name);
  }

  auto cb = [name_str = std::string(name), this]() {
    ProtocolConverter::structBegin(absl::string_view(name_str));
  };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::structEnd() {
  if (requestStarted()) {
    return ProtocolConverter::structEnd();
  }

  auto cb = [this]() { ProtocolConverter::structEnd(); };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::fieldBegin(absl::string_view name, FieldType& field_type,
                                          int16_t& field_id) {
  if (requestStarted()) {
    return ProtocolConverter::fieldBegin(name, field_type, field_id);
  }

  auto cb = [name_str = std::string(name), field_type, field_id, this]() mutable {
    ProtocolConverter::fieldBegin(absl::string_view(name_str), field_type, field_id);
  };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::fieldEnd() {
  if (requestStarted()) {
    return ProtocolConverter::fieldEnd();
  }

  auto cb = [this]() { ProtocolConverter::fieldEnd(); };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::boolValue(bool& value) {
  if (requestStarted()) {
    return ProtocolConverter::boolValue(value);
  }

  auto cb = [value, this]() mutable { ProtocolConverter::boolValue(value); };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::byteValue(uint8_t& value) {
  if (requestStarted()) {
    return ProtocolConverter::byteValue(value);
  }

  auto cb = [value, this]() mutable { ProtocolConverter::byteValue(value); };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::int16Value(int16_t& value) {
  if (requestStarted()) {
    return ProtocolConverter::int16Value(value);
  }

  auto cb = [value, this]() mutable { ProtocolConverter::int16Value(value); };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::int32Value(int32_t& value) {
  if (requestStarted()) {
    return ProtocolConverter::int32Value(value);
  }

  auto cb = [value, this]() mutable { ProtocolConverter::int32Value(value); };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::int64Value(int64_t& value) {
  if (requestStarted()) {
    return ProtocolConverter::int64Value(value);
  }

  auto cb = [value, this]() mutable { ProtocolConverter::int64Value(value); };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::doubleValue(double& value) {
  if (requestStarted()) {
    return ProtocolConverter::doubleValue(value);
  }

  auto cb = [value, this]() mutable { ProtocolConverter::doubleValue(value); };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::stringValue(absl::string_view value) {
  if (requestStarted()) {
    return ProtocolConverter::stringValue(value);
  }

  auto cb = [value_str = std::string(value), this]() {
    ProtocolConverter::stringValue(absl::string_view(value_str));
  };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::mapBegin(FieldType& key_type, FieldType& value_type,
                                        uint32_t& size) {
  if (requestStarted()) {
    return ProtocolConverter::mapBegin(key_type, value_type, size);
  }

  auto cb = [key_type, value_type, size, this]() mutable {
    ProtocolConverter::mapBegin(key_type, value_type, size);
  };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::mapEnd() {
  if (requestStarted()) {
    return ProtocolConverter::mapEnd();
  }

  auto cb = [this]() { ProtocolConverter::mapEnd(); };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::listBegin(FieldType& elem_type, uint32_t& size) {
  if (requestStarted()) {
    return ProtocolConverter::listBegin(elem_type, size);
  }

  auto cb = [elem_type, size, this]() mutable { ProtocolConverter::listBegin(elem_type, size); };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::listEnd() {
  if (requestStarted()) {
    return ProtocolConverter::listEnd();
  }

  auto cb = [this]() { ProtocolConverter::listEnd(); };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::setBegin(FieldType& elem_type, uint32_t& size) {
  if (requestStarted()) {
    return ProtocolConverter::setBegin(elem_type, size);
  }

  auto cb = [elem_type, size, this]() mutable { ProtocolConverter::setBegin(elem_type, size); };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::setEnd() {
  if (requestStarted()) {
    return ProtocolConverter::setEnd();
  }

  auto cb = [this]() { ProtocolConverter::setEnd(); };
  pending_callbacks_.push_back(std::move(cb));

  return FilterStatus::Continue;
}

FilterStatus ShadowRouterImpl::messageEnd() {
  auto cb = [this]() {
    ASSERT(upstream_request_->conn_data_ != nullptr);

    ProtocolConverter::messageEnd();
    const auto encode_size = upstream_request_->encodeAndWrite(upstream_request_buffer_);
    addSize(encode_size);
    recordUpstreamRequestSize(*cluster_, request_size_);

    request_sent_ = true;

    if (metadata_->messageType() == MessageType::Oneway) {
      upstream_request_->releaseConnection(false);
    }
  };

  if (requestStarted()) {
    cb();
  } else {
    request_ready_ = true;
    pending_callbacks_.push_back(std::move(cb));
  }

  return FilterStatus::Continue;
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
  if (router_destroyed_) {
    upstream_request_.reset();
    if (inserted()) {
      removeFromList(parent_.active_routers_);
    }
  }
}

void ShadowRouterImpl::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  const bool done =
      upstream_request_->handleUpstreamData(data, end_stream, *this, *upstream_response_callbacks_);
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
