#pragma once

#include "envoy/server/status_manager.h"

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Server {

class StatusManagerImpl : public StatusManager {
public:
  void addComponent(absl::string_view component) override;
  void updateStatus(absl::string_view component, std::unique_ptr<StatusHandle>&& status) override;
  StatusHandle status(absl::string_view component) override;

private:
  using StatusMap = absl::flat_hash_map<std::string, std::unique_ptr<StatusManager::StatusHandle>>;
  absl::Mutex status_map_lock_;
  StatusMap status_map_;
};

} // namespace Server
} // namespace Envoy
