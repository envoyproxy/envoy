#pragma once

#include "envoy/network/io_handle.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

/**
 * Redirect records to be used in connections.
 */
class Win32RedirectRecordsFilterState : public StreamInfo::FilterState::Object {
public:
  Win32RedirectRecordsFilterState(std::shared_ptr<Network::Win32RedirectRecords> records)
      : records_(records) {}
  const std::shared_ptr<Network::Win32RedirectRecords> value() const { return records_; }
  static const std::string& key();

private:
  const std::shared_ptr<Network::Win32RedirectRecords> records_;
};

} // namespace Network
} // namespace Envoy
