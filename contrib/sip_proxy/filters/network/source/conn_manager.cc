#include "contrib/sip_proxy/filters/network/source/conn_manager.h"

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"

#include "source/common/tracing/http_tracer_impl.h"

#include "contrib/sip_proxy/filters/network/source/app_exception_impl.h"
#include "contrib/sip_proxy/filters/network/source/encoder.h"
#include "router/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

TrafficRoutingAssistantHandler::TrafficRoutingAssistantHandler(
    ConnectionManager& parent, Event::Dispatcher& dispatcher,
    const envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceConfig& config,
    Server::Configuration::FactoryContext& context, StreamInfo::StreamInfoImpl& stream_info)
    : parent_(parent), stream_info_(std::move(stream_info)) {

  if (config.has_grpc_service()) {
    const std::chrono::milliseconds timeout =
        std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, timeout, 2000));
    tra_client_ =
        TrafficRoutingAssistant::traClient(dispatcher, context, config.grpc_service(), timeout);
    tra_client_->setRequestCallbacks(*this);
  }
}

void TrafficRoutingAssistantHandler::updateTrafficRoutingAssistant(
    const std::string& type, const std::string& key, const std::string& val,
    const absl::optional<TraContextMap> context) {
  if (cache_manager_[type][key] != val) {
    cache_manager_.insertCache(type, key, val);
    if (traClient()) {
      traClient()->updateTrafficRoutingAssistant(
          type, absl::flat_hash_map<std::string, std::string>{std::make_pair(key, val)}, context,
          Tracing::NullSpan::instance(), stream_info_);
    }
  }
}

QueryStatus TrafficRoutingAssistantHandler::retrieveTrafficRoutingAssistant(
    const std::string& type, const std::string& key, const absl::optional<TraContextMap> context,
    SipFilters::DecoderFilterCallbacks& activetrans, std::string& host) {
  if (cache_manager_.contains(type, key)) {
    host = cache_manager_[type][key];
    return QueryStatus::Continue;
  }

  if (activetrans.metadata()->affinityIteration()->query()) {
    parent_.pushIntoPendingList(type, key, activetrans, [&]() {
      if (traClient()) {
        traClient()->retrieveTrafficRoutingAssistant(type, key, context,
                                                     Tracing::NullSpan::instance(), stream_info_);
      }
    });
    host = "";
    return QueryStatus::Pending;
  }
  host = "";
  return QueryStatus::Stop;
}

void TrafficRoutingAssistantHandler::deleteTrafficRoutingAssistant(
    const std::string& type, const std::string& key, const absl::optional<TraContextMap> context) {
  cache_manager_[type].erase(key);
  if (traClient()) {
    traClient()->deleteTrafficRoutingAssistant(type, key, context, Tracing::NullSpan::instance(),
                                               stream_info_);
  }
}

void TrafficRoutingAssistantHandler::subscribeTrafficRoutingAssistant(const std::string& type) {
  if (traClient()) {
    traClient()->subscribeTrafficRoutingAssistant(type, Tracing::NullSpan::instance(),
                                                  stream_info_);
  }
}

void TrafficRoutingAssistantHandler::complete(const TrafficRoutingAssistant::ResponseType& type,
                                              const std::string& message_type,
                                              const absl::any& resp) {
  switch (type) {
  case TrafficRoutingAssistant::ResponseType::CreateResp: {
    ENVOY_LOG(trace, "TRA === CreateResp");
    break;
  }
  case TrafficRoutingAssistant::ResponseType::UpdateResp: {
    ENVOY_LOG(trace, "TRA === UpdateResp");
    break;
  }
  case TrafficRoutingAssistant::ResponseType::RetrieveResp: {
    auto resp_data =
        absl::any_cast<
            envoy::extensions::filters::network::sip_proxy::tra::v3alpha::RetrieveResponse>(resp)
            .data();
    for (const auto& item : resp_data) {
      ENVOY_LOG(trace, "TRA === RetrieveResp {} {}={}", message_type, item.first, item.second);
      if (!item.second.empty()) {
        parent_.onResponseHandleForPendingList(
            message_type, item.first,
            [&](MessageMetadataSharedPtr metadata, DecoderEventHandler& decoder_event_handler) {
              cache_manager_[message_type].emplace(item.first, item.second);
              metadata->setDestination(item.second);
              return parent_.continueHandling(metadata, decoder_event_handler);
            });
      }

      // If the wrong response received, then try next affinity
      parent_.onResponseHandleForPendingList(
          message_type, item.first,
          [&](MessageMetadataSharedPtr metadata, DecoderEventHandler& decoder_event_handler) {
            metadata->nextAffinityIteration();
            parent_.continueHandling(metadata, decoder_event_handler);
          });
    }

    break;
  }
  case TrafficRoutingAssistant::ResponseType::DeleteResp: {
    ENVOY_LOG(trace, "TRA === DeleteResp");
    break;
  }
  case TrafficRoutingAssistant::ResponseType::SubscribeResp: {
    ENVOY_LOG(trace, "TRA === SubscribeResp");
    auto data =
        absl::any_cast<
            envoy::extensions::filters::network::sip_proxy::tra::v3alpha::SubscribeResponse>(resp)
            .data();
    for (auto& item : data) {
      ENVOY_LOG(debug, "TRA UPDATE {}: {}={}", message_type, item.first, item.second);
      cache_manager_[message_type].emplace(item.first, item.second);
    }
  }
  default:
    break;
  }
}

void TrafficRoutingAssistantHandler::doSubscribe(
    const envoy::extensions::filters::network::sip_proxy::v3alpha::CustomizedAffinity&
        customized_affinity) {
  for (const auto& aff : customized_affinity.entries()) {
    if (aff.subscribe() == true &&
        is_subscribe_map_.find(aff.key_name()) == is_subscribe_map_.end()) {
      subscribeTrafficRoutingAssistant(aff.key_name());
      is_subscribe_map_[aff.key_name()] = true;
    }

    if (aff.cache().max_cache_item() > 0) {
      cache_manager_.initCache(aff.key_name(), aff.cache().max_cache_item());
    }
  }
}

ConnectionManager::ConnectionManager(
    Config& config, Random::RandomGenerator& random_generator, TimeSource& time_source,
    Server::Configuration::FactoryContext& context,
    std::shared_ptr<Router::TransactionInfos> transaction_infos,
    std::shared_ptr<SipProxy::DownstreamConnectionInfos> downstream_connection_infos,
    std::shared_ptr<SipProxy::UpstreamTransactionInfos> upstream_transaction_info)
    : config_(config), stats_(config_.stats()), decoder_(std::make_unique<Decoder>(*this)),
      random_generator_(random_generator), time_source_(time_source), context_(context),
      transaction_infos_(transaction_infos),
      downstream_connection_infos_(downstream_connection_infos),
      upstream_transaction_infos_(upstream_transaction_info) {}

ConnectionManager::~ConnectionManager() = default;

Network::FilterStatus ConnectionManager::onNewConnection() {
  std::string thread_id = this->context_.api().threadFactory().currentThreadId().debugString() +
                          "@" +
                          read_callbacks_->connection()
                              .connectionInfoProvider()
                              .localAddress()
                              ->ip()
                              ->addressAsString();
  std::string downstream_conn_id =
      read_callbacks_->connection().connectionInfoProvider().directRemoteAddress()->asString() +
      "@" + random_generator_.uuid();
  local_origin_ingress_ = OriginIngress(thread_id, downstream_conn_id);
  downstream_connection_infos_->insertDownstreamConnection(downstream_conn_id, *this);

  ENVOY_LOG(info, "Created downstream connection with thread_id={}, downstream_connection_id={}",
            thread_id, downstream_conn_id);
  ENVOY_LOG(debug, "Number of downstream connections={}", downstream_connection_infos_->size());

  return Network::FilterStatus::Continue;
}

Network::FilterStatus ConnectionManager::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(
      debug, "sip proxy received data {} --> {} bytes {}", read_callbacks_->connection(),
      read_callbacks_->connection().connectionInfoProvider().remoteAddress()->asStringView(),
      read_callbacks_->connection().connectionInfoProvider().localAddress()->asStringView(),
      data.length());
  request_buffer_.move(data);
  dispatch();

  if (end_stream) {
    ENVOY_CONN_LOG(info, "downstream half-closed", read_callbacks_->connection());

    resetAllTrans(false);
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }

  return Network::FilterStatus::StopIteration;
}

void ConnectionManager::continueHandling(const std::string& key, bool try_next_affinity) {
  onResponseHandleForPendingList(
      "connection_pending", key,
      [&](MessageMetadataSharedPtr metadata, DecoderEventHandler& decoder_event_handler) {
        if (try_next_affinity) {
          metadata->nextAffinityIteration();
          if (metadata->affinityIteration() != metadata->affinity().end()) {
            metadata->setState(State::HandleAffinity);
            continueHandling(metadata, decoder_event_handler);
          } else {
            // When onPoolFailure, continueHandling with try_next_affinity, but there is no next
            // affinity, need throw exception and response with 503.
            auto ex = AppException(AppExceptionType::InternalError,
                                   fmt::format("envoy can't establish connection to {}", key));
            sendLocalReply(*(metadata), ex, false);
            setLocalResponseSent(metadata->transactionId().value());

            decoder_->complete();
          }
        } else {
          continueHandling(metadata, decoder_event_handler);
        }
      });
}

void ConnectionManager::continueHandling(MessageMetadataSharedPtr metadata,
                                         DecoderEventHandler& decoder_event_handler) {
  try {
    decoder_->restore(metadata, decoder_event_handler);
    decoder_->onData(request_buffer_, true);
  } catch (const AppException& ex) {
    ENVOY_LOG(debug, "sip application exception: {}", ex.what());
    sendLocalReply(*(decoder_->metadata()), ex, false);
    setLocalResponseSent(decoder_->metadata()->transactionId().value());

    decoder_->complete();
  } catch (const EnvoyException& ex) {
    ENVOY_CONN_LOG(debug, "sip error: {}", read_callbacks_->connection(), ex.what());

    // Still unaware how to handle this, just close the connection
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }
}

void ConnectionManager::dispatch() {
  try {
    decoder_->onData(request_buffer_);
  } catch (const AppException& ex) {
    ENVOY_LOG(debug, "sip application exception: {}", ex.what());
    sendLocalReply(*(decoder_->metadata()), ex, false);
    setLocalResponseSent(decoder_->metadata()->transactionId().value());

    decoder_->complete();
  } catch (const EnvoyException& ex) {
    ENVOY_CONN_LOG(debug, "sip error: {}", read_callbacks_->connection(), ex.what());

    // Still unaware how to handle this, just close the connection
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }
}

void ConnectionManager::sendLocalReply(MessageMetadata& metadata, const DirectResponse& response,
                                       bool end_stream) {
  if (read_callbacks_->connection().state() == Network::Connection::State::Closed) {
    ENVOY_LOG(debug, "Connection state is closed");
    return;
  }

  Buffer::OwnedImpl buffer;

  metadata.setEP(Utility::localAddress(context_));
  const DirectResponse::ResponseType result = response.encode(metadata, buffer);

  ENVOY_CONN_LOG(
      debug, "send local reply {} --> {} bytes {}\n{}", read_callbacks_->connection(),
      read_callbacks_->connection().connectionInfoProvider().localAddress()->asStringView(),
      read_callbacks_->connection().connectionInfoProvider().remoteAddress()->asStringView(),
      buffer.length(), buffer.toString());

  read_callbacks_->connection().write(buffer, end_stream);
  if (end_stream) {
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }

  switch (result) {
  case DirectResponse::ResponseType::SuccessReply:
    stats_.response_success_.inc();
    break;
  case DirectResponse::ResponseType::ErrorReply:
    stats_.response_error_.inc();
    break;
  case DirectResponse::ResponseType::Exception:
    stats_.response_exception_.inc();
    break;
  default:
    PANIC("not reached");
  }
  stats_.counterFromElements("", "local-generated-response").inc();
}

void ConnectionManager::setLocalResponseSent(absl::string_view transaction_id) {
  if (transactions_.find(transaction_id) != transactions_.end()) {
    transactions_[transaction_id]->setLocalResponseSent(true);
  }
}

void ConnectionManager::doDeferredTransDestroy(ConnectionManager::ActiveTrans& trans) {
  read_callbacks_->connection().dispatcher().deferredDelete(
      std::move(transactions_.at(trans.transactionId())));
  transactions_.erase(trans.transactionId());
}

void ConnectionManager::doDeferredUpstreamTransDestroy(
    ConnectionManager::UpstreamActiveTrans& trans) {
  upstream_transaction_infos_->deleteTransaction(trans.transactionId());
}

void ConnectionManager::resetAllTrans(bool local_reset) {
  ENVOY_LOG(info, "active_trans to be deleted {}", transactions_.size());
  for (auto it = transactions_.cbegin(); it != transactions_.cend();) {
    if (local_reset) {
      ENVOY_CONN_LOG(debug, "local close with active request", read_callbacks_->connection());
      stats_.cx_destroy_local_with_active_rq_.inc();
    } else {
      ENVOY_CONN_LOG(debug, "remote close with active request", read_callbacks_->connection());
      stats_.cx_destroy_remote_with_active_rq_.inc();
    }

    (it++)->second->onReset();
  }
}

void ConnectionManager::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;

  read_callbacks_->connection().addConnectionCallbacks(*this);
  read_callbacks_->connection().enableHalfClose(true);

  auto stream_info = StreamInfo::StreamInfoImpl(
      time_source_, read_callbacks_->connection().connectionInfoProviderSharedPtr());
  tra_handler_ = std::make_shared<TrafficRoutingAssistantHandler>(
      *this, read_callbacks_->connection().dispatcher(), config_.settings()->traServiceConfig(),
      context_, stream_info);
}

void ConnectionManager::onEvent(Network::ConnectionEvent event) {
  ENVOY_CONN_LOG(info, "received event {}", read_callbacks_->connection(), static_cast<int>(event));
  resetAllTrans(event == Network::ConnectionEvent::LocalClose);

  if (local_origin_ingress_.has_value() && ((event == Network::ConnectionEvent::RemoteClose) ||
                                        (event == Network::ConnectionEvent::LocalClose))) {
    downstream_connection_infos_->deleteDownstreamConnection(
        local_origin_ingress_->getDownstreamConnectionID());
    ENVOY_LOG(debug, "Deleted from Downstream connection map {}: {}\n",
              downstream_connection_infos_, local_origin_ingress_->getDownstreamConnectionID());
  }
}

DecoderEventHandler& ConnectionManager::newDecoderEventHandler(MessageMetadataSharedPtr metadata) {
  std::string&& k = std::string(metadata->transactionId().value());

  if ((metadata->msgType() == MsgType::Response) &&
      (upstream_transaction_infos_->hasTransaction(k))) {
    ENVOY_LOG(debug, "Response from upstream transaction ID {} received.", k);
    return *(upstream_transaction_infos_->getTransaction(k));
  }

  stats_.request_active_.inc();
  
  stats_.counterFromElements(methodStr[metadata->methodType()], "request_received").inc();

  // if (metadata->methodType() == MethodType::Ack) {
  if (transactions_.find(k) != transactions_.end()) {
    // ACK_4XX metadata will updated later.
    return *transactions_.at(k);
  }
  // }

  ActiveTransPtr new_trans = std::make_unique<DownstreamActiveTrans>(*this, metadata);
  new_trans->createFilterChain();
  transactions_.emplace(k, std::move(new_trans));

  return *transactions_.at(k);
}

bool ConnectionManager::ResponseDecoder::onData(MessageMetadataSharedPtr metadata) {
  metadata_ = metadata;
  if (auto status = transportBegin(metadata_); status == FilterStatus::StopIteration) {
    return true;
  }

  if (auto status = messageBegin(metadata_); status == FilterStatus::StopIteration) {
    return true;
  }

  if (auto status = messageEnd(); status == FilterStatus::StopIteration) {
    return true;
  }

  if (auto status = transportEnd(); status == FilterStatus::StopIteration) {
    return true;
  }

  return true;
}

FilterStatus ConnectionManager::ResponseDecoder::messageBegin(MessageMetadataSharedPtr metadata) {
  UNREFERENCED_PARAMETER(metadata);
  return FilterStatus::Continue;
}

FilterStatus ConnectionManager::ResponseDecoder::messageEnd() { return FilterStatus::Continue; }

FilterStatus ConnectionManager::ResponseDecoder::transportEnd() {
  ASSERT(metadata_ != nullptr);

  ConnectionManager& cm = parent_.parent_;

  if (cm.read_callbacks_->connection().state() == Network::Connection::State::Closed) {
    throw EnvoyException("downstream connection is closed");
  }

  Buffer::OwnedImpl buffer;

  metadata_->setEP(Utility::localAddress(cm.context_));
  std::shared_ptr<Encoder> encoder = std::make_shared<EncoderImpl>();

  encoder->encode(metadata_, buffer);

  ENVOY_STREAM_LOG(debug, "send response {}\n{}", parent_, buffer.length(), buffer.toString());
  cm.read_callbacks_->connection().write(buffer, false);

  cm.stats_.response_.inc();
  cm.stats_.counterFromElements(methodStr[metadata_->methodType()], "response_proxied").inc();

  return FilterStatus::Continue;
}

FilterStatus ConnectionManager::ActiveTrans::applyDecoderFilters(ActiveTransDecoderFilter* filter) {
  ASSERT(filter_action_ != nullptr);

  if (!local_response_sent_) {
    std::list<ActiveTransDecoderFilterPtr>::iterator entry;
    if (!filter) {
      entry = decoder_filters_.begin();
    } else {
      entry = std::next(filter->entry());
    }

    for (; entry != decoder_filters_.end(); entry++) {
      const FilterStatus status = filter_action_((*entry)->handle_.get());
      if (local_response_sent_) {
        // The filter called sendLocalReply: stop processing filters and return
        // FilterStatus::Continue irrespective of the current result.
        break;
      }

      if (status != FilterStatus::Continue) {
        return status;
      }
    }
  }

  filter_action_ = nullptr;
  filter_context_.reset();

  return FilterStatus::Continue;
}

FilterStatus ConnectionManager::ActiveTrans::transportBegin(MessageMetadataSharedPtr metadata) {
  if (local_response_sent_) {
    ENVOY_LOG(debug, "Message after local 503 message, return directly");
    return FilterStatus::StopIteration;
  }

  metadata_ = metadata;
  filter_context_ = metadata;
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    MessageMetadataSharedPtr metadata = absl::any_cast<MessageMetadataSharedPtr>(filter_context_);
    return filter->transportBegin(metadata);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveTrans::transportEnd() {
  ASSERT(metadata_ != nullptr);
  parent_.stats_.request_.inc();

  FilterStatus status;
  filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus {
    return filter->transportEnd();
  };

  status = applyDecoderFilters(nullptr);
  if (status == FilterStatus::StopIteration) {
    return status;
  }

  finalizeRequest();

  return status;
}

void ConnectionManager::ActiveTrans::finalizeRequest() {}

FilterStatus ConnectionManager::ActiveTrans::messageBegin(MessageMetadataSharedPtr metadata) {
  filter_context_ = metadata;
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    MessageMetadataSharedPtr metadata = absl::any_cast<MessageMetadataSharedPtr>(filter_context_);
    return filter->messageBegin(metadata);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveTrans::messageEnd() {
  filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus { return filter->messageEnd(); };
  return applyDecoderFilters(nullptr);
}

void ConnectionManager::ActiveTrans::createFilterChain() {
  parent_.config_.filterFactory().createFilterChain(*this);
}

void ConnectionManager::ActiveTrans::onReset() { parent_.doDeferredTransDestroy(*this); }

void ConnectionManager::ActiveTrans::onError(const std::string& what) {
  if (metadata_) {
    sendLocalReply(AppException(AppExceptionType::ProtocolError, what), false);
    return;
  }

  parent_.doDeferredTransDestroy(*this);
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

const Network::Connection* ConnectionManager::ActiveTrans::connection() const {
  return &parent_.read_callbacks_->connection();
}

Router::RouteConstSharedPtr ConnectionManager::ActiveTrans::route() {
  if (!cached_route_) {
    if (metadata_ != nullptr) {
      Router::RouteConstSharedPtr route = parent_.config_.routerConfig().route(*metadata_);
      cached_route_ = std::move(route);
    } else {
      cached_route_ = nullptr;
    }
  }
  return cached_route_.value();
}

void ConnectionManager::ActiveTrans::sendLocalReply(const DirectResponse& response,
                                                    bool end_stream) {
  parent_.sendLocalReply(*metadata_, response, end_stream);

  if (end_stream) {
    return;
  }

  // Consume any remaining request data from the downstream.
  local_response_sent_ = true;
}

void ConnectionManager::ActiveTrans::startUpstreamResponse() {
  response_decoder_ = std::make_unique<ResponseDecoder>(*this);
}

SipFilters::ResponseStatus ConnectionManager::ActiveTrans::upstreamData(
    MessageMetadataSharedPtr metadata, Router::RouteConstSharedPtr return_route,
    const std::string& return_destination, Network::Connection* return_connection) {
  ASSERT(response_decoder_ != nullptr);
  UNREFERENCED_PARAMETER(return_route);
  UNREFERENCED_PARAMETER(return_destination);
  UNREFERENCED_PARAMETER(return_connection);

  try {
    if (response_decoder_->onData(metadata)) {
      // Completed upstream response.
      // parent_.doDeferredRpcDestroy(*this);
      return SipFilters::ResponseStatus::Complete;
    }
    return SipFilters::ResponseStatus::MoreData;
  } catch (const AppException& ex) {
    ENVOY_LOG(error, "sip response application error: {}", ex.what());

    sendLocalReply(ex, false);
    return SipFilters::ResponseStatus::Reset;
  } catch (const EnvoyException& ex) {
    ENVOY_CONN_LOG(error, "sip response error: {}", parent_.read_callbacks_->connection(),
                   ex.what());

    onError(ex.what());
    return SipFilters::ResponseStatus::Reset;
  }
}

void ConnectionManager::ActiveTrans::resetDownstreamConnection() {
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

// start of upstreamActiveTrans

void ConnectionManager::UpstreamActiveTrans::onReset() {}

void ConnectionManager::UpstreamActiveTrans::onError(const std::string& what) {
  if (metadata_) {
    sendLocalReply(AppException(AppExceptionType::ProtocolError, what), true);
    return;
  }
  parent_.doDeferredUpstreamTransDestroy(*this);
}

const Network::Connection* ConnectionManager::UpstreamActiveTrans::connection() const {
  return &parent_.read_callbacks_->connection();
}

void ConnectionManager::UpstreamActiveTrans::sendLocalReply(const DirectResponse& response,
                                                            bool end_stream) {
  Buffer::OwnedImpl buffer;

  metadata_->setEP(
      return_connection_->connectionInfoProvider().localAddress()->ip()->addressAsString());
  const DirectResponse::ResponseType result = response.encode(*metadata_, buffer);

  ENVOY_CONN_LOG(debug, "send upstream local reply {} --> {} bytes {}\n{}", *return_connection_,
                 return_connection_->connectionInfoProvider().localAddress()->asStringView(),
                 return_connection_->connectionInfoProvider().remoteAddress()->asStringView(),
                 buffer.length(), buffer.toString());

  return_connection_->write(buffer, false);

  // TODO implement stats related to upstream requests
  switch (result) {
  case DirectResponse::ResponseType::SuccessReply:
    stats().upstream_response_success_.inc();
    break;
  case DirectResponse::ResponseType::ErrorReply:
    stats().upstream_response_error_.inc();
    break;
  case DirectResponse::ResponseType::Exception:
    stats().upstream_response_exception_.inc();
    break;
  default:
    PANIC("not reached");
  }
  stats().counterFromElements("", "upstream-local-generated-response").inc();

  // Consume any remaining request data from the upstream.
  local_response_sent_ = end_stream;
}

void ConnectionManager::UpstreamActiveTrans::startUpstreamResponse() {
  ASSERT(false, "startUpstreamResponse() Not implemented");
}

SipFilters::ResponseStatus ConnectionManager::UpstreamActiveTrans::upstreamData(
    MessageMetadataSharedPtr metadata, Router::RouteConstSharedPtr return_route,
    const std::string& return_destination, Network::Connection* return_connection) {
  route_ = return_route;
  destination_ = return_destination;
  return_connection_ = return_connection;
  metadata_ = metadata;

  if (local_response_sent_) {
    ENVOY_LOG(error, "Message after local response sent closing the transaction, return directly");
    return SipFilters::ResponseStatus::Reset;
  }

  try {
    if (parent_.read_callbacks_->connection().state() == Network::Connection::State::Closed) {
      throw EnvoyException("Downstream connection is closed");
    }

    Buffer::OwnedImpl buffer;
    std::unique_ptr<Encoder> encoder = std::make_unique<EncoderImpl>();
    encoder->encode(metadata, buffer);

    ENVOY_LOG(debug, "Sending upstream request downstream to {}. {} bytes \n{}",
              parent_.local_origin_ingress_->getDownstreamConnectionID(), buffer.length(),
              buffer.toString());
    parent_.read_callbacks_->connection().write(buffer, false);

    return SipFilters::ResponseStatus::Complete;
  } catch (const AppException& ex) {
    ENVOY_LOG(error, "SIP response application error: {}", ex.what());
    sendLocalReply(ex, false);
    return SipFilters::ResponseStatus::Reset;
  } catch (const EnvoyException& ex) {
    ENVOY_CONN_LOG(error, "SIP response error: {}", parent_.read_callbacks_->connection(),
                   ex.what());
    onError(ex.what());
    return SipFilters::ResponseStatus::Reset;
  }
}

void ConnectionManager::UpstreamActiveTrans::resetDownstreamConnection() {
  ENVOY_LOG(info, "JONAH = Unexpected call to UpstreamActiveTrans::resetDownstreamConnection");
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

// start of DownstreamConnection

ConnectionManager::DownstreamConnection::DownstreamConnection(
    ConnectionManager& parent)
    : parent_(parent),
      stream_info_(parent.time_source_,
                   parent.read_callbacks_->connection().connectionInfoProviderSharedPtr()) {}

const Network::Connection* ConnectionManager::DownstreamConnection::connection() const {
  return &parent_.read_callbacks_->connection();
}

SipFilterStats& ConnectionManager::DownstreamConnection::stats() {
  return parent_.config_.stats();
}

std::shared_ptr<SipSettings> ConnectionManager::DownstreamConnection::settings() const {
  return parent_.config_.settings();
}

void ConnectionManager::DownstreamConnection::startUpstreamResponse() {}

SipFilters::ResponseStatus ConnectionManager::DownstreamConnection::upstreamData(
    MessageMetadataSharedPtr metadata, Router::RouteConstSharedPtr return_route,
    const std::string& return_destination, Network::Connection* return_connection) {
  std::string&& k = std::string(metadata->transactionId().value());

  if (upstreamTransactionInfo()->hasTransaction(k)) {
    ;
    auto active_trans = upstreamTransactionInfo()->getTransaction(k);
    return active_trans->upstreamData(metadata, return_route, return_destination,
                                      return_connection);
  }

  auto active_trans = std::make_shared<UpstreamActiveTrans>(parent_, metadata);
  active_trans->createFilterChain();
  upstreamTransactionInfo()->insertTransaction(k, active_trans);

  // keep the counter happy to avoid crashes when the UpstreamActiveTrans decrements
  // Todo decide what if this should be a different stat..
  // parent_.stats_.request_active_.inc();

  return active_trans->upstreamData(metadata, return_route, return_destination, return_connection);
}

void DownstreamConnectionInfos::init() {
  // Note: `this` and `cluster_name` have a lifetime of the filter.
  // That may be shorter than the tls callback if the listener is torn down shortly after it is
  // created. We use a weak pointer to make sure this object outlives the tls callbacks.
  std::weak_ptr<DownstreamConnectionInfos> this_weak_ptr = this->shared_from_this();
  tls_->set(
      [this_weak_ptr](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
        UNREFERENCED_PARAMETER(dispatcher); // todo
        if (auto this_shared_ptr = this_weak_ptr.lock()) {
          return std::make_shared<ThreadLocalDownstreamConnectionInfo>(this_shared_ptr);
        }
        return nullptr;
      });
}

void DownstreamConnectionInfos::insertDownstreamConnection(std::string conn_id,
                                                           ConnectionManager& conn_manager) {
  if (hasDownstreamConnection(conn_id)) {
    return;
  }

  tls_->getTyped<ThreadLocalDownstreamConnectionInfo>().downstream_connection_info_map_.emplace(
      std::make_pair(conn_id, std::make_shared<ConnectionManager::DownstreamConnection>(
                                  conn_manager)));
}

size_t DownstreamConnectionInfos::size() {
  return tls_->getTyped<ThreadLocalDownstreamConnectionInfo>()
      .downstream_connection_info_map_.size();
}

void DownstreamConnectionInfos::deleteDownstreamConnection(std::string&& conn_id) {
  if (hasDownstreamConnection(conn_id)) {
    tls_->getTyped<ThreadLocalDownstreamConnectionInfo>().downstream_connection_info_map_.erase(
        conn_id);
  }
}

bool DownstreamConnectionInfos::hasDownstreamConnection(std::string& conn_id) {
  return tls_->getTyped<ThreadLocalDownstreamConnectionInfo>()
      .downstream_connection_info_map_.contains(conn_id);
}

SipFilters::DecoderFilterCallbacks&
DownstreamConnectionInfos::getDownstreamConnection(std::string& conn_id) {
  return *(tls_->getTyped<ThreadLocalDownstreamConnectionInfo>().downstream_connection_info_map_.at(
      conn_id));
}

std::string DownstreamConnectionInfos::dumpDownstreamConnection() {
  std::stringstream output;
  absl::flat_hash_map<std::string, std::shared_ptr<SipFilters::DecoderFilterCallbacks>> dc_map =
      tls_->getTyped<ThreadLocalDownstreamConnectionInfo>().downstream_connection_info_map_;
  for (auto element = dc_map.begin(); element != dc_map.end(); element++) {
    output << "   Downstream Connection ID: '" << element->first
           << "'; Downstream Connection pointer '" << element->second << "'\n";
  }
  return output.str();
}

void SipProxy::ThreadLocalUpstreamTransactionInfo::auditTimerAction() {
  const auto p1 = dispatcher_.timeSource().systemTime();
  for (auto it = upstream_transaction_infos_map_.cbegin();
       it != upstream_transaction_infos_map_.cend();) {
    auto key = it->first;
    auto trans_to_end = it->second;
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        p1 - trans_to_end->streamInfo().startTime());
    if (diff.count() >= transaction_timeout_.count()) {
      it++;
      trans_to_end->onReset();
      upstream_transaction_infos_map_.erase(key);
      ENVOY_LOG(info, "Removing from cache upstream transaction with ID {} due to timeout reached",
                key);
      continue;
    }
    it++;
  }
  audit_timer_->enableTimer(std::chrono::seconds(2));
}

void SipProxy::UpstreamTransactionInfos::init() {
  // Note: `this` and `cluster_name` have a a lifetime of the filter.
  // That may be shorter than the tls callback if the listener is torn down shortly after it is
  // created. We use a weak pointer to make sure this object outlives the tls callbacks.
  std::weak_ptr<UpstreamTransactionInfos> this_weak_ptr = this->shared_from_this();
  tls_->set(
      [this_weak_ptr](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
        if (auto this_shared_ptr = this_weak_ptr.lock()) {
          return std::make_shared<ThreadLocalUpstreamTransactionInfo>(
              this_shared_ptr, dispatcher, this_shared_ptr->transaction_timeout_);
        }
        return nullptr;
      });
}

void SipProxy::UpstreamTransactionInfos::insertTransaction(
    std::string transaction_id,
    std::shared_ptr<ConnectionManager::UpstreamActiveTrans> active_trans) {
  ENVOY_LOG(debug, "Inserting into cache upstream transaction with ID {} ... ", transaction_id);
  if (hasTransaction(transaction_id)) {
    return;
  }

  tls_->getTyped<ThreadLocalUpstreamTransactionInfo>().upstream_transaction_infos_map_.emplace(
      std::make_pair(transaction_id, active_trans));
}

void SipProxy::UpstreamTransactionInfos::deleteTransaction(std::string&& transaction_id) {
  ENVOY_LOG(debug, "Deleting from cache upstream transaction with ID {} ... ", transaction_id);
  if (hasTransaction(transaction_id)) {
    getTransaction(transaction_id)->onReset();
    tls_->getTyped<ThreadLocalUpstreamTransactionInfo>().upstream_transaction_infos_map_.erase(
        transaction_id);
  }
}

bool SipProxy::UpstreamTransactionInfos::hasTransaction(std::string& transaction_id) {
  return tls_->getTyped<ThreadLocalUpstreamTransactionInfo>().upstream_transaction_infos_map_.find(
             transaction_id) != tls_->getTyped<ThreadLocalUpstreamTransactionInfo>()
                                    .upstream_transaction_infos_map_.end();
}

std::shared_ptr<SipProxy::ConnectionManager::UpstreamActiveTrans>
SipProxy::UpstreamTransactionInfos::getTransaction(std::string& transaction_id) {
  return tls_->getTyped<ThreadLocalUpstreamTransactionInfo>().upstream_transaction_infos_map_.at(
      transaction_id);
}

size_t SipProxy::UpstreamTransactionInfos::size() {
  return tls_->getTyped<ThreadLocalUpstreamTransactionInfo>()
      .upstream_transaction_infos_map_.size();
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
