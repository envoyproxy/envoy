#include "extensions/filters/network/rocketmq_proxy/conn_manager.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/common/enum_to_int.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

ConsumerGroupMember::ConsumerGroupMember(absl::string_view client_id,
                                         ConnectionManager& conn_manager)
    : client_id_(client_id.data(), client_id.size()), connection_manager_(&conn_manager),
      last_(connection_manager_->time_source_.monotonicTime()) {}

void ConsumerGroupMember::refresh() { last_ = connection_manager_->time_source_.monotonicTime(); }

bool ConsumerGroupMember::expired() const {
  auto duration = connection_manager_->time_source_.monotonicTime() - last_;
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() >
         connection_manager_->config().transientObjectLifeSpan().count();
}

ConnectionManager::ConnectionManager(Config& config, TimeSource& time_source)
    : config_(config), time_source_(time_source), stats_(config.stats()) {}

Envoy::Network::FilterStatus ConnectionManager::onData(Envoy::Buffer::Instance& data,
                                                       bool end_stream) {
  ENVOY_CONN_LOG(trace, "rocketmq_proxy: received {} bytes.", read_callbacks_->connection(),
                 data.length());
  request_buffer_.move(data);
  dispatch();
  if (end_stream) {
    resetAllActiveMessages("Connection to downstream is closed");
    read_callbacks_->connection().close(Envoy::Network::ConnectionCloseType::FlushWrite);
  }
  return Network::FilterStatus::StopIteration;
}

void ConnectionManager::dispatch() {
  if (request_buffer_.length() < Decoder::MIN_FRAME_SIZE) {
    ENVOY_CONN_LOG(warn, "rocketmq_proxy: request buffer length is less than min frame size: {}",
                   read_callbacks_->connection(), request_buffer_.length());
    return;
  }

  bool underflow = false;
  bool has_decode_error = false;
  while (!underflow) {
    RemotingCommandPtr request = Decoder::decode(request_buffer_, underflow, has_decode_error);
    if (underflow) {
      // Wait for more data
      break;
    }
    stats_.request_.inc();

    // Decode error, we need to close connection immediately.
    if (has_decode_error) {
      ENVOY_CONN_LOG(error, "Failed to decode request, close connection immediately",
                     read_callbacks_->connection());
      stats_.request_decoding_error_.inc();
      resetAllActiveMessages("Failed to decode data from downstream. Close connection immediately");
      read_callbacks_->connection().close(Envoy::Network::ConnectionCloseType::FlushWrite);
      return;
    } else {
      stats_.request_decoding_success_.inc();
    }

    switch (static_cast<RequestCode>(request->code())) {
    case RequestCode::GetRouteInfoByTopic: {
      ENVOY_CONN_LOG(trace, "GetTopicRoute request, code: {}, opaque: {}",
                     read_callbacks_->connection(), request->code(), request->opaque());
      onGetTopicRoute(std::move(request));
    } break;

    case RequestCode::UnregisterClient: {
      ENVOY_CONN_LOG(trace, "process unregister client request, code: {}, opaque: {}",
                     read_callbacks_->connection(), request->code(), request->opaque());
      onUnregisterClient(std::move(request));
    } break;

    case RequestCode::SendMessage: {
      ENVOY_CONN_LOG(trace, "SendMessage request, code: {}, opaque: {}",
                     read_callbacks_->connection(), request->code(), request->opaque());
      onSendMessage(std::move(request));
      stats_.send_message_v1_.inc();
    } break;

    case RequestCode::SendMessageV2: {
      ENVOY_CONN_LOG(trace, "SendMessage request, code: {}, opaque: {}",
                     read_callbacks_->connection(), request->code(), request->opaque());
      onSendMessage(std::move(request));
      stats_.send_message_v2_.inc();
    } break;

    case RequestCode::GetConsumerListByGroup: {
      ENVOY_CONN_LOG(trace, "GetConsumerListByGroup request, code: {}, opaque: {}",
                     read_callbacks_->connection(), request->code(), request->opaque());
      onGetConsumerListByGroup(std::move(request));
    } break;

    case RequestCode::PopMessage: {
      ENVOY_CONN_LOG(trace, "PopMessage request, code: {}, opaque: {}",
                     read_callbacks_->connection(), request->code(), request->opaque());
      onPopMessage(std::move(request));
      stats_.pop_message_.inc();
    } break;

    case RequestCode::AckMessage: {
      ENVOY_CONN_LOG(trace, "AckMessage request, code: {}, opaque: {}",
                     read_callbacks_->connection(), request->code(), request->opaque());
      onAckMessage(std::move(request));
      stats_.ack_message_.inc();
    } break;

    case RequestCode::HeartBeat: {
      ENVOY_CONN_LOG(trace, "Heartbeat request, opaque: {}", read_callbacks_->connection(),
                     request->opaque());
      onHeartbeat(std::move(request));
    } break;

    default: {
      ENVOY_CONN_LOG(warn, "Request code {} not supported yet", read_callbacks_->connection(),
                     request->code());
      std::string error_msg("Request not supported");
      onError(request, error_msg);
    } break;
    }
  }
}

void ConnectionManager::purgeDirectiveTable() {
  auto current = time_source_.monotonicTime();
  for (auto it = ack_directive_table_.begin(); it != ack_directive_table_.end();) {
    auto duration = current - it->second.creation_time_;
    if (std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() >
        config_.transientObjectLifeSpan().count()) {
      ack_directive_table_.erase(it++);
    } else {
      it++;
    }
  }
}

void ConnectionManager::sendResponseToDownstream(RemotingCommandPtr& response) {
  Buffer::OwnedImpl buffer;
  Encoder::encode(response, buffer);
  if (read_callbacks_->connection().state() == Network::Connection::State::Open) {
    ENVOY_CONN_LOG(trace, "Write response to downstream. Opaque: {}", read_callbacks_->connection(),
                   response->opaque());
    read_callbacks_->connection().write(buffer, false);
  } else {
    ENVOY_CONN_LOG(error, "Send response to downstream failed as connection is no longer open",
                   read_callbacks_->connection());
  }
}

void ConnectionManager::onGetTopicRoute(RemotingCommandPtr request) {
  createActiveMessage(request).onQueryTopicRoute();
  stats_.get_topic_route_.inc();
}

void ConnectionManager::onHeartbeat(RemotingCommandPtr request) {
  const std::string& body = request->body().toString();

  purgeDirectiveTable();

  ProtobufWkt::Struct body_struct;
  try {
    MessageUtil::loadFromJson(body, body_struct);
  } catch (std::exception& e) {
    ENVOY_LOG(warn, "Failed to decode heartbeat body. Error message: {}", e.what());
    return;
  }

  HeartbeatData heartbeatData;
  if (!heartbeatData.decode(body_struct)) {
    ENVOY_LOG(warn, "Failed to decode heartbeat data");
    return;
  }

  for (const auto& group : heartbeatData.consumerGroups()) {
    addOrUpdateGroupMember(group, heartbeatData.clientId());
  }

  RemotingCommandPtr response = std::make_unique<RemotingCommand>();
  response->code(enumToSignedInt(ResponseCode::Success));
  response->opaque(request->opaque());
  response->remark("Heartbeat OK");
  response->markAsResponse();
  sendResponseToDownstream(response);
  stats_.heartbeat_.inc();
}

void ConnectionManager::addOrUpdateGroupMember(absl::string_view group,
                                               absl::string_view client_id) {
  ENVOY_LOG(trace, "#addOrUpdateGroupMember. Group: {}, client ID: {}", group, client_id);
  auto search = group_members_.find(std::string(group.data(), group.length()));
  if (search == group_members_.end()) {
    std::vector<ConsumerGroupMember> members;
    members.emplace_back(ConsumerGroupMember(client_id, *this));
    group_members_.emplace(std::string(group.data(), group.size()), members);
  } else {
    std::vector<ConsumerGroupMember>& members = search->second;
    for (auto it = members.begin(); it != members.end();) {
      if (it->clientId() == client_id) {
        it->refresh();
        ++it;
      } else if (it->expired()) {
        it = members.erase(it);
      } else {
        ++it;
      }
    }
    if (members.empty()) {
      group_members_.erase(search);
    }
  }
}

void ConnectionManager::onUnregisterClient(RemotingCommandPtr request) {
  auto header = request->typedCustomHeader<UnregisterClientRequestHeader>();
  ASSERT(header != nullptr);
  ASSERT(!header->clientId().empty());
  ENVOY_LOG(trace, "Unregister client ID: {}, producer group: {}, consumer group: {}",
            header->clientId(), header->producerGroup(), header->consumerGroup());

  if (!header->consumerGroup().empty()) {
    auto search = group_members_.find(header->consumerGroup());
    if (search != group_members_.end()) {
      std::vector<ConsumerGroupMember>& members = search->second;
      for (auto it = members.begin(); it != members.end();) {
        if (it->clientId() == header->clientId()) {
          it = members.erase(it);
        } else if (it->expired()) {
          it = members.erase(it);
        } else {
          ++it;
        }
      }
      if (members.empty()) {
        group_members_.erase(search);
      }
    }
  }

  RemotingCommandPtr response = std::make_unique<RemotingCommand>(
      enumToSignedInt(ResponseCode::Success), request->version(), request->opaque());
  response->markAsResponse();
  response->remark("Envoy unregister client OK.");
  sendResponseToDownstream(response);
  stats_.unregister_.inc();
}

void ConnectionManager::onError(RemotingCommandPtr& request, absl::string_view error_msg) {
  Buffer::OwnedImpl buffer;
  RemotingCommandPtr response = std::make_unique<RemotingCommand>();
  response->markAsResponse();
  response->opaque(request->opaque());
  response->code(enumToSignedInt(ResponseCode::SystemError));
  response->remark(error_msg);
  sendResponseToDownstream(response);
}

void ConnectionManager::onSendMessage(RemotingCommandPtr request) {
  ENVOY_CONN_LOG(trace, "#onSendMessage, opaque: {}", read_callbacks_->connection(),
                 request->opaque());
  auto header = request->typedCustomHeader<SendMessageRequestHeader>();
  header->queueId(-1);
  createActiveMessage(request).sendRequestToUpstream();
}

void ConnectionManager::onGetConsumerListByGroup(RemotingCommandPtr request) {
  auto requestExtHeader = request->typedCustomHeader<GetConsumerListByGroupRequestHeader>();

  ASSERT(requestExtHeader != nullptr);
  ASSERT(!requestExtHeader->consumerGroup().empty());

  ENVOY_LOG(trace, "#onGetConsumerListByGroup, consumer group: {}",
            requestExtHeader->consumerGroup());

  auto search = group_members_.find(requestExtHeader->consumerGroup());
  GetConsumerListByGroupResponseBody getConsumerListByGroupResponseBody;
  if (search != group_members_.end()) {
    std::vector<ConsumerGroupMember>& members = search->second;
    std::sort(members.begin(), members.end());
    for (const auto& member : members) {
      getConsumerListByGroupResponseBody.add(member.clientId());
    }
  } else {
    ENVOY_LOG(warn, "There is no consumer belongs to consumer_group: {}",
              requestExtHeader->consumerGroup());
  }
  ProtobufWkt::Struct body_struct;

  getConsumerListByGroupResponseBody.encode(body_struct);

  RemotingCommandPtr response = std::make_unique<RemotingCommand>(
      enumToSignedInt(ResponseCode::Success), request->version(), request->opaque());
  response->markAsResponse();
  std::string json = MessageUtil::getJsonStringFromMessage(body_struct);
  response->body().add(json);
  ENVOY_LOG(trace, "GetConsumerListByGroup respond with body: {}", json);

  sendResponseToDownstream(response);
  stats_.get_consumer_list_.inc();
}

void ConnectionManager::onPopMessage(RemotingCommandPtr request) {
  auto header = request->typedCustomHeader<PopMessageRequestHeader>();
  ASSERT(header != nullptr);
  ENVOY_LOG(trace, "#onPopMessage. Consumer group: {}, topic: {}", header->consumerGroup(),
            header->topic());
  createActiveMessage(request).sendRequestToUpstream();
}

void ConnectionManager::onAckMessage(RemotingCommandPtr request) {
  auto header = request->typedCustomHeader<AckMessageRequestHeader>();
  ASSERT(header != nullptr);
  ENVOY_LOG(
      trace,
      "#onAckMessage. Consumer group: {}, topic: {}, queue Id: {}, offset: {}, extra-info: {}",
      header->consumerGroup(), header->topic(), header->queueId(), header->offset(),
      header->extraInfo());

  // Fill the target broker_name and broker_id routing directive
  auto it = ack_directive_table_.find(header->directiveKey());
  if (it == ack_directive_table_.end()) {
    ENVOY_LOG(warn, "There was no previous ack directive available, which is unexpected");
    onError(request, "No ack directive is found");
    return;
  }
  header->targetBrokerName(it->second.broker_name_);
  header->targetBrokerId(it->second.broker_id_);

  createActiveMessage(request).sendRequestToUpstream();
}

ActiveMessage& ConnectionManager::createActiveMessage(RemotingCommandPtr& request) {
  ENVOY_CONN_LOG(trace, "ConnectionManager#createActiveMessage. Code: {}, opaque: {}",
                 read_callbacks_->connection(), request->code(), request->opaque());
  ActiveMessagePtr active_message = std::make_unique<ActiveMessage>(*this, std::move(request));
  active_message->moveIntoList(std::move(active_message), active_message_list_);
  return **active_message_list_.begin();
}

void ConnectionManager::deferredDelete(ActiveMessage& active_message) {
  read_callbacks_->connection().dispatcher().deferredDelete(
      active_message.removeFromList(active_message_list_));
}

void ConnectionManager::resetAllActiveMessages(absl::string_view error_msg) {
  while (!active_message_list_.empty()) {
    ENVOY_CONN_LOG(warn, "Reset pending request {} due to error: {}", read_callbacks_->connection(),
                   active_message_list_.front()->downstreamRequest()->opaque(), error_msg);
    active_message_list_.front()->onReset();
    stats_.response_error_.inc();
  }
}

Envoy::Network::FilterStatus ConnectionManager::onNewConnection() {
  return Network::FilterStatus::Continue;
}

void ConnectionManager::initializeReadFilterCallbacks(
    Envoy::Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
