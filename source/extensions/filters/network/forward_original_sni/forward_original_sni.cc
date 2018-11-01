#include "extensions/filters/network/forward_original_sni/forward_original_sni.h"

#include "envoy/network/connection.h"

#include "common/stream_info/forward_requested_server_name.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ForwardOriginalSni {

using ::Envoy::StreamInfo::ForwardRequestedServerName;

Network::FilterStatus ForwardOriginalSniFilter::onNewConnection() {
  absl::string_view sni = read_callbacks_->connection().requestedServerName();

  if (!sni.empty()) {
    read_callbacks_->connection().streamInfo().filterState().setData(
        ForwardRequestedServerName::Key, std::make_unique<ForwardRequestedServerName>(sni),
        StreamInfo::FilterState::StateType::ReadOnly);
  }

  return Network::FilterStatus::Continue;
}

} // namespace ForwardOriginalSni
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
