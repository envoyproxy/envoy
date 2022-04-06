#include "source/extensions/filters/network/packet_trace/packet_trace.h"

#include "envoy/buffer/buffer.h"
#include "envoy/extensions/filters/network/packet_trace/v3/packet_trace.pb.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PacketTrace {

PacketTraceFilter::PacketTraceFilter(PacketTraceConfigSharedPtr config)
    : config_(std::move(config)) {}

Network::FilterStatus PacketTraceFilter::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(debug, "packet trace filter, data len: {}, end_stream: {}",
                 read_callbacks_->connection(), data.length(), end_stream);
  auto local_address = config_->packetTraceLocalAddress();
  auto remote_address = config_->packetTraceRemoteAddress();
  if (((local_address.getIpListSize() == 0) ||
       (local_address.contains(
           *(read_callbacks_->connection().connectionInfoProvider().localAddress())))) &&
      ((remote_address.getIpListSize() == 0 ||
        (remote_address.contains(
            *(read_callbacks_->connection().connectionInfoProvider().remoteAddress())))))) {
    auto src_port =
        read_callbacks_->connection().connectionInfoProvider().remoteAddress()->ip()->port();
    ProtobufWkt::Struct metadata;
    auto& fields = *metadata.mutable_fields();
    auto val = ProtobufWkt::Value();
    val.set_number_value(src_port);
    fields.insert({NetworkFilterNames::get().PacketTrace, val});
    read_callbacks_->connection().streamInfo().setDynamicMetadata(
        NetworkFilterNames::get().DownToUp, metadata);
  }
  return Network::FilterStatus::Continue;
}

} // namespace PacketTrace
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
