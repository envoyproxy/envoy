#pragma once

#include "envoy/network/io_handle.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

/**
 * Redirect records to be used in connections.
 */
class RedirectRecordsFilterState : public StreamInfo::FilterState::Object {
public:
  RedirectRecordsFilterState(Network::EnvoyRedirectRecords records) : records_(records) {}
  const Network::EnvoyRedirectRecords& value() const { return records_; }
  static const std::string& key();

private:
  const Network::EnvoyRedirectRecords records_;
};

} // namespace Network
} // namespace Envoy
