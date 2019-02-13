#include "extensions/transport_sockets/tap/tap_config_impl.h"

#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

PerSocketTapperImpl::PerSocketTapperImpl(SocketTapConfigImplSharedPtr config,
                                         const Network::Connection& connection)
    : config_(std::move(config)), connection_(connection), statuses_(config_->numMatchers()),
      trace_(std::make_shared<envoy::data::tap::v2alpha::BufferedTraceWrapper>()) {
  config_->rootMatcher().onNewStream(statuses_);
}

void PerSocketTapperImpl::closeSocket(Network::ConnectionEvent) {
  if (!config_->rootMatcher().matches(statuses_)) {
    return;
  }

  auto* connection = trace_->mutable_socket_buffered_trace()->mutable_connection();
  connection->set_id(connection_.id());
  Network::Utility::addressToProtobufAddress(*connection_.localAddress(),
                                             *connection->mutable_local_address());
  Network::Utility::addressToProtobufAddress(*connection_.remoteAddress(),
                                             *connection->mutable_remote_address());
  config_->submitBufferedTrace(trace_, connection_.id());
}

void PerSocketTapperImpl::onRead(const Buffer::Instance& data, uint32_t bytes_read) {
  if (!config_->rootMatcher().matches(statuses_)) {
    return;
  }

  auto* event = trace_->mutable_socket_buffered_trace()->add_events();
  event->mutable_timestamp()->MergeFrom(Protobuf::util::TimeUtil::NanosecondsToTimestamp(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          config_->time_source_.systemTime().time_since_epoch())
          .count()));
  // TODO(mattklein123): Avoid linearize/toString here.
  // TODO(mattklein123): Implement truncation configuration. Also figure out how/if truncation
  // applies to multiple event messages if we are above our limit.
  const std::string linearized_data = data.toString();
  event->mutable_read()->mutable_data()->set_as_bytes(
      linearized_data.data() + (linearized_data.size() - bytes_read), bytes_read);
}

void PerSocketTapperImpl::onWrite(const Buffer::Instance& data, uint32_t bytes_written,
                                  bool end_stream) {
  if (!config_->rootMatcher().matches(statuses_)) {
    return;
  }

  auto* event = trace_->mutable_socket_buffered_trace()->add_events();
  event->mutable_timestamp()->MergeFrom(Protobuf::util::TimeUtil::NanosecondsToTimestamp(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          config_->time_source_.systemTime().time_since_epoch())
          .count()));
  // TODO(mattklein123): Avoid linearize/toString here.
  // TODO(mattklein123): Implement truncation configuration. Also figure out how/if truncation
  // applies to multiple event messages if we are above our limit.
  const std::string linearized_data = data.toString();
  event->mutable_write()->mutable_data()->set_as_bytes(linearized_data.data(), bytes_written);
  event->mutable_write()->set_end_stream(end_stream);
}

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
