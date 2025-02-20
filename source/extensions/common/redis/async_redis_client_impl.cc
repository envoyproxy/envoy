#include "source/extensions/common/redis/async_redis_client_impl.h"

#include "source/common/upstream/cluster_manager_impl.h"
#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Redis {

RedisAsyncClient::RedisAsyncClient(Tcp::AsyncTcpClientPtr&& tcp_client, Upstream::ClusterManager& cluster_manager) : tcp_client_(std::move(tcp_client)), decoder_(*this), cluster_manager_(cluster_manager) {
    tcp_client_->setAsyncTcpClientCallbacks(*this);
}

void RedisAsyncClient::onEvent(Network::ConnectionEvent event) {
    if (event == Network::ConnectionEvent::RemoteClose || 
    event == Network::ConnectionEvent::LocalClose) {
    if (callback_) {
    callback_(false, false /*ignored*/, absl::nullopt/*ignored*/);
    callback_ = nullptr; 
    }

    // Iterate over all queued requests and call a callback 
    // indicating that connection failed. They would ost likely fail as well.
    // A subsequent request
    // will trigger the connection process again.
  for (; !queue_.empty(); queue_.pop()){
        std::get<2>(queue_.front())(false, false, absl::nullopt);
    }
  waiting_for_response_ = false;
    } 
}

void RedisAsyncClient::onData(Buffer::Instance& buf, bool) {
  NetworkFilters::Common::Redis::RespValue response;
  decoder_.decode(buf);
  waiting_for_response_ = false;

  if (!queue_.empty()) {
    auto& element = queue_.front();
    write(*(std::get<0>(element)), std::get<1>(element), std::move(std::get<2>(element)));
    queue_.pop();
  }
}

void RedisAsyncClient::onRespValue(NetworkFilters::Common::Redis::RespValuePtr&& value) {
  if (value->type() == NetworkFilters::Common::Redis::RespType::Null) {
    callback_(true, false, absl::nullopt);
  } else {
    std::string response = value->toString();
    // Result is a string containing quotes. Drop the quotes on both sides of the string.
    response = response.substr(1, response.length() - 2);
    callback_(true, true, std::move(response));
  }
  callback_ = nullptr;

  value.reset();
}

  void RedisAsyncClient::write(Buffer::Instance& data, bool end_stream,  ResultCallback&& result_callback) {
    if (!tcp_client_->connected()) {
    tcp_client_->connect();
    }

    // No synch requires, because this is never executed on different threads.
    if (waiting_for_response_) {
        // Queue the request.
        std::unique_ptr<Buffer::OwnedImpl> queued_data = std::make_unique<Buffer::OwnedImpl>();
        queued_data->add(data);
       // No sync is required to insert and remove objects into the queue, as 
      // RedisAsyncClient is thread local object and those operations are executed on the same thread.
       queue_.emplace(std::move(queued_data), end_stream, std::move(result_callback)); 
       return;      
    }

    waiting_for_response_ = true;
    callback_ = std::move(result_callback);
    tcp_client_->write(data, end_stream);
  }
} // namespace Redis
} // namespace Common
} // namespace Extensions
} // namespace Envoy
