#include "extensions/filters/network/original_sni/original_sni.h"

#include "envoy/network/connection.h"

#include "common/stream_info/original_requested_server_name.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace OriginalSni {

typedef ::Envoy::StreamInfo::OriginalRequestedServerName OriginalRequestedServerName;

Network::FilterStatus OriginalSniFilter::onNewConnection() {
  absl::string_view sni = read_callbacks_->connection().requestedServerName();

  if (!sni.empty()) {
    read_callbacks_->connection().streamInfo().filterState().setData(
        OriginalRequestedServerName::Key, std::make_unique<OriginalRequestedServerName>(sni));
  }

  return Network::FilterStatus::Continue;
}

} // namespace OriginalSni
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
