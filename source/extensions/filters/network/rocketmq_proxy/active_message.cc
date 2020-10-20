#include "extensions/filters/network/rocketmq_proxy/active_message.h"

#include "envoy/upstream/cluster_manager.h"

#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/network/rocketmq_proxy/conn_manager.h"
#include "extensions/filters/network/rocketmq_proxy/topic_route.h"
#include "extensions/filters/network/rocketmq_proxy/well_known_names.h"
#include "extensions/filters/network/well_known_names.h"

#include "absl/strings/match.h"

using Envoy::Tcp::ConnectionPool::ConnectionDataPtr;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

ActiveMessage::ActiveMessage(ConnectionManager& conn_manager, RemotingCommandPtr&& request)
    : connection_manager_(conn_manager), request_(std::move(request)) {
  metadata_ = std::make_shared<MessageMetadata>();
  MetadataHelper::parseRequest(request_, metadata_);
  updateActiveRequestStats();
}

ActiveMessage::~ActiveMessage() { updateActiveRequestStats(false); }

void ActiveMessage::createFilterChain() { router_ = connection_manager_.config().createRouter(); }

void ActiveMessage::sendRequestToUpstream() {
  if (!router_) {
    createFilterChain();
  }
  router_->sendRequestToUpstream(*this);
}

Router::RouteConstSharedPtr ActiveMessage::route() {
  if (cached_route_) {
    return cached_route_.value();
  }
  const std::string& topic_name = metadata_->topicName();
  ENVOY_LOG(trace, "fetch route for topic: {}", topic_name);
  Router::RouteConstSharedPtr route = connection_manager_.config().routerConfig().route(*metadata_);
  cached_route_ = route;
  return cached_route_.value();
}

void ActiveMessage::onError(absl::string_view error_message) {
  connection_manager_.onError(request_, error_message);
}

const RemotingCommandPtr& ActiveMessage::downstreamRequest() const { return request_; }

void ActiveMessage::fillAckMessageDirective(Buffer::Instance& buffer, const std::string& group,
                                            const std::string& topic,
                                            const AckMessageDirective& directive) {
  int32_t cursor = 0;
  const int32_t buffer_length = buffer.length();
  while (cursor < buffer_length) {
    auto frame_length = buffer.peekBEInt<int32_t>(cursor);
    std::string decoded_topic = Decoder::decodeTopic(buffer, cursor);
    ENVOY_LOG(trace, "Process a message: consumer group: {}, topic: {}, messageId: {}",
              decoded_topic, group, Decoder::decodeMsgId(buffer, cursor));
    if (!absl::StartsWith(decoded_topic, RetryTopicPrefix) && decoded_topic != topic) {
      ENVOY_LOG(warn,
                "Decoded topic from pop-response does not equal to request. Decoded topic: "
                "{}, request topic: {}, message ID: {}",
                decoded_topic, topic, Decoder::decodeMsgId(buffer, cursor));
    }

    /*
     * Sometimes, client SDK may used -1 for queue-id in the pop request so that broker servers
     * are allowed to lookup all queues it serves. So we need to use the actual queue Id from
     * response body.
     */
    int32_t queue_id = Decoder::decodeQueueId(buffer, cursor);
    int64_t queue_offset = Decoder::decodeQueueOffset(buffer, cursor);

    std::string key = fmt::format("{}-{}-{}-{}", group, decoded_topic, queue_id, queue_offset);
    connection_manager_.insertAckDirective(key, directive);
    ENVOY_LOG(
        debug,
        "Insert an ack directive. Consumer group: {}, topic: {}, queue Id: {}, queue offset: {}",
        group, topic, queue_id, queue_offset);
    cursor += frame_length;
  }
}

void ActiveMessage::sendResponseToDownstream() {
  if (request_->code() == enumToSignedInt(RequestCode::PopMessage)) {
    // Fill ack message directive
    auto pop_header = request_->typedCustomHeader<PopMessageRequestHeader>();
    AckMessageDirective directive(pop_header->targetBrokerName(), pop_header->targetBrokerId(),
                                  connection_manager_.timeSource().monotonicTime());
    ENVOY_LOG(trace, "Receive pop response from broker name: {}, broker ID: {}",
              pop_header->targetBrokerName(), pop_header->targetBrokerId());
    fillAckMessageDirective(response_->body(), pop_header->consumerGroup(), pop_header->topic(),
                            directive);
  }

  // If acknowledgment of the message is successful, we need to erase the ack directive from
  // manager.
  if (request_->code() == enumToSignedInt(RequestCode::AckMessage) &&
      response_->code() == enumToSignedInt(ResponseCode::Success)) {
    auto ack_header = request_->typedCustomHeader<AckMessageRequestHeader>();
    connection_manager_.eraseAckDirective(ack_header->directiveKey());
  }

  if (response_) {
    response_->opaque(request_->opaque());
    connection_manager_.sendResponseToDownstream(response_);
  }
}

void ActiveMessage::fillBrokerData(std::vector<BrokerData>& list, const std::string& cluster,
                                   const std::string& broker_name, int64_t broker_id,
                                   const std::string& address) {
  bool found = false;
  for (auto& entry : list) {
    if (entry.cluster() == cluster && entry.brokerName() == broker_name) {
      found = true;
      if (entry.brokerAddresses().find(broker_id) != entry.brokerAddresses().end()) {
        ENVOY_LOG(warn, "Duplicate broker_id found. Broker ID: {}, address: {}", broker_id,
                  address);
        continue;
      } else {
        entry.brokerAddresses()[broker_id] = address;
      }
    }
  }

  if (!found) {
    absl::node_hash_map<int64_t, std::string> addresses;
    addresses.emplace(broker_id, address);

    list.emplace_back(BrokerData(cluster, broker_name, std::move(addresses)));
  }
}

void ActiveMessage::onQueryTopicRoute() {
  std::string cluster_name;
  ASSERT(metadata_->hasTopicName());
  const std::string& topic_name = metadata_->topicName();
  Upstream::ThreadLocalCluster* cluster = nullptr;
  Router::RouteConstSharedPtr route = this->route();
  if (route) {
    cluster_name = route->routeEntry()->clusterName();
    Upstream::ClusterManager& cluster_manager = connection_manager_.config().clusterManager();
    cluster = cluster_manager.get(cluster_name);
  }
  if (cluster) {
    ENVOY_LOG(trace, "Envoy has an operating cluster {} for topic {}", cluster_name, topic_name);
    std::vector<QueueData> queue_data_list;
    std::vector<BrokerData> broker_data_list;
    for (auto& host_set : cluster->prioritySet().hostSetsPerPriority()) {
      if (host_set->hosts().empty()) {
        continue;
      }
      for (const auto& host : host_set->hosts()) {
        std::string broker_address = host->address()->asString();
        auto& filter_metadata = host->metadata()->filter_metadata();
        const auto filter_it = filter_metadata.find(NetworkFilterNames::get().RocketmqProxy);
        ASSERT(filter_it != filter_metadata.end());
        const auto& metadata_fields = filter_it->second.fields();
        ASSERT(metadata_fields.contains(RocketmqConstants::get().BrokerName));
        std::string broker_name =
            metadata_fields.at(RocketmqConstants::get().BrokerName).string_value();
        ASSERT(metadata_fields.contains(RocketmqConstants::get().ClusterName));
        std::string broker_cluster_name =
            metadata_fields.at(RocketmqConstants::get().ClusterName).string_value();
        // Proto3 will ignore the field if the value is zero.
        int32_t read_queue_num = 0;
        if (metadata_fields.contains(RocketmqConstants::get().ReadQueueNum)) {
          read_queue_num = static_cast<int32_t>(
              metadata_fields.at(RocketmqConstants::get().WriteQueueNum).number_value());
        }
        int32_t write_queue_num = 0;
        if (metadata_fields.contains(RocketmqConstants::get().WriteQueueNum)) {
          write_queue_num = static_cast<int32_t>(
              metadata_fields.at(RocketmqConstants::get().WriteQueueNum).number_value());
        }
        int32_t perm = 0;
        if (metadata_fields.contains(RocketmqConstants::get().Perm)) {
          perm = static_cast<int32_t>(
              metadata_fields.at(RocketmqConstants::get().Perm).number_value());
        }
        int32_t broker_id = 0;
        if (metadata_fields.contains(RocketmqConstants::get().BrokerId)) {
          broker_id = static_cast<int32_t>(
              metadata_fields.at(RocketmqConstants::get().BrokerId).number_value());
        }
        queue_data_list.emplace_back(QueueData(broker_name, read_queue_num, write_queue_num, perm));
        if (connection_manager_.config().developMode()) {
          ENVOY_LOG(trace, "Develop mode, return proxy address to replace all broker addresses so "
                           "that L4 network rewrite is not required");
          fillBrokerData(broker_data_list, broker_cluster_name, broker_name, broker_id,
                         connection_manager_.config().proxyAddress());
        } else {
          fillBrokerData(broker_data_list, broker_cluster_name, broker_name, broker_id,
                         broker_address);
        }
      }
    }
    ENVOY_LOG(trace, "Prepare TopicRouteData for {} OK", topic_name);
    TopicRouteData topic_route_data(std::move(queue_data_list), std::move(broker_data_list));
    ProtobufWkt::Struct data_struct;
    topic_route_data.encode(data_struct);
    std::string json = MessageUtil::getJsonStringFromMessage(data_struct);
    ENVOY_LOG(trace, "Serialize TopicRouteData for {} OK:\n{}", cluster_name, json);
    RemotingCommandPtr response = std::make_unique<RemotingCommand>(
        static_cast<int>(ResponseCode::Success), downstreamRequest()->version(),
        downstreamRequest()->opaque());
    response->markAsResponse();
    response->body().add(json);
    connection_manager_.sendResponseToDownstream(response);
  } else {
    onError("Cluster is not available");
    ENVOY_LOG(warn, "Cluster for topic {} is not available", topic_name);
  }
  onReset();
}

void ActiveMessage::onReset() { connection_manager_.deferredDelete(*this); }

bool ActiveMessage::onUpstreamData(Envoy::Buffer::Instance& data, bool end_stream,
                                   ConnectionDataPtr& conn_data) {
  bool underflow = false;
  bool has_error = false;
  response_ = Decoder::decode(data, underflow, has_error, downstreamRequest()->code());
  if (underflow && !end_stream) {
    ENVOY_LOG(trace, "Wait for more data from upstream");
    return false;
  }

  if (enumToSignedInt(RequestCode::PopMessage) == request_->code() && router_ != nullptr) {
    recordPopRouteInfo(router_->upstreamHost());
  }

  connection_manager_.stats().response_.inc();
  if (!has_error) {
    connection_manager_.stats().response_decoding_success_.inc();
    // Relay response to downstream
    sendResponseToDownstream();
  } else {
    ENVOY_LOG(error, "Failed to decode response for opaque: {}, close immediately.",
              downstreamRequest()->opaque());
    onError("Failed to decode response from upstream");
    connection_manager_.stats().response_decoding_error_.inc();
    conn_data->connection().close(Network::ConnectionCloseType::NoFlush);
  }

  if (end_stream) {
    conn_data->connection().close(Network::ConnectionCloseType::NoFlush);
  }
  return true;
}

void ActiveMessage::recordPopRouteInfo(Upstream::HostDescriptionConstSharedPtr host_description) {
  if (host_description) {
    auto host_metadata = host_description->metadata();
    auto filter_metadata = host_metadata->filter_metadata();
    const auto filter_it = filter_metadata.find(NetworkFilterNames::get().RocketmqProxy);
    ASSERT(filter_it != filter_metadata.end());
    const auto& metadata_fields = filter_it->second.fields();
    ASSERT(metadata_fields.contains(RocketmqConstants::get().BrokerName));
    std::string broker_name =
        metadata_fields.at(RocketmqConstants::get().BrokerName).string_value();
    // Proto3 will ignore the field if the value is zero.
    int32_t broker_id = 0;
    if (metadata_fields.contains(RocketmqConstants::get().BrokerId)) {
      broker_id = static_cast<int32_t>(
          metadata_fields.at(RocketmqConstants::get().BrokerId).number_value());
    }
    // Tag the request with upstream host metadata: broker-name, broker-id
    auto custom_header = request_->typedCustomHeader<CommandCustomHeader>();
    custom_header->targetBrokerName(broker_name);
    custom_header->targetBrokerId(broker_id);
  }
}

void ActiveMessage::updateActiveRequestStats(bool is_inc) {
  if (is_inc) {
    connection_manager_.stats().request_active_.inc();
  } else {
    connection_manager_.stats().request_active_.dec();
  }
  auto code = static_cast<RequestCode>(request_->code());
  switch (code) {
  case RequestCode::PopMessage: {
    if (is_inc) {
      connection_manager_.stats().pop_message_active_.inc();
    } else {
      connection_manager_.stats().pop_message_active_.dec();
    }
    break;
  }
  case RequestCode::SendMessage: {
    if (is_inc) {
      connection_manager_.stats().send_message_v1_active_.inc();
    } else {
      connection_manager_.stats().send_message_v1_active_.dec();
    }
    break;
  }
  case RequestCode::SendMessageV2: {
    if (is_inc) {
      connection_manager_.stats().send_message_v2_active_.inc();
    } else {
      connection_manager_.stats().send_message_v2_active_.dec();
    }
    break;
  }
  case RequestCode::GetRouteInfoByTopic: {
    if (is_inc) {
      connection_manager_.stats().get_topic_route_active_.inc();
    } else {
      connection_manager_.stats().get_topic_route_active_.dec();
    }
    break;
  }
  default:
    break;
  }
}

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
