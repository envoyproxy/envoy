#pragma once

#include <list>

#include "envoy/api/v2/discovery.pb.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Config {

struct UpdateAck {
  UpdateAck(absl::string_view nonce, absl::string_view type_url)
      : nonce_(nonce), type_url_(type_url) {}
  std::string nonce_;
  std::string type_url_;
  ::google::rpc::Status error_detail_;
};

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
  void pop();
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
