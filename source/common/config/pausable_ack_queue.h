#pragma once

#include <list>

#include "common/config/update_ack.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Config {

// There is a head-of-line blocking issue resulting from the intersection of 1) ADS's need for
// subscription request ordering and 2) the ability to "pause" one of the resource types within ADS.
// We need a queue that understands ADS's resource type pausing. Specifically, we need front()/pop()
// to choose the first element whose type_url isn't paused.
class PausableAckQueue {
public:
  void push(UpdateAck x);
  size_t size() const;
  bool empty();
  const UpdateAck& front();
  UpdateAck popFront();
  void pause(const std::string& type_url);
  void resume(const std::string& type_url);
  bool paused(const std::string& type_url) const;

private:
  // It's ok for non-existent subs to be paused/resumed. The cleanest way to support that is to give
  // the pause state its own map. (Map key is type_url.)
  absl::flat_hash_map<std::string, bool> paused_;
  std::list<UpdateAck> storage_;
};

} // namespace Config
} // namespace Envoy
