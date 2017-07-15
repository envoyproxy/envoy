#pragma once

#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Upstream {

/**
 * Utility class for handling member update callbacks.
 */
class MemberUpdateCbHelper {
public:
  /**
   * Add a member update callback.
   * @param callback supplies the callback to add.
   * @return const MemberUpdateCbHandle* a handle that can be used to remove the callback via
   *         remove().
   */
  const HostSet::MemberUpdateCbHandle* add(HostSet::MemberUpdateCb callback) {
    callbacks_.emplace_back(*this, callback);
    return &callbacks_.back();
  }

  /**
   * Remove a member update callback added via add().
   * @param handle supplies the callback handle to remove.
   */
  void remove(const HostSet::MemberUpdateCbHandle* handle) {
    ASSERT(std::find_if(callbacks_.begin(), callbacks_.end(),
                        [handle](const UpdateCbHolder& holder) -> bool {
                          return handle == &holder;
                        }) != callbacks_.end());
    callbacks_.remove_if(
        [handle](const UpdateCbHolder& holder) -> bool { return handle == &holder; });
  }

  /**
   * Run all registered callbacks.
   * @param hosts_added supplies the added hosts.
   * @param hosts_removed supplies the removed hosts.
   */
  void runCallbacks(const std::vector<HostSharedPtr>& hosts_added,
                    const std::vector<HostSharedPtr>& hosts_removed) {
    for (const auto& cb : callbacks_) {
      cb.cb_(hosts_added, hosts_removed);
    }
  }

private:
  struct UpdateCbHolder : public HostSet::MemberUpdateCbHandle {
    UpdateCbHolder(MemberUpdateCbHelper& parent, HostSet::MemberUpdateCb cb)
        : parent_(parent), cb_(cb) {}

    MemberUpdateCbHelper& parent_;
    HostSet::MemberUpdateCb cb_;
  };

  std::list<UpdateCbHolder> callbacks_;
};

} // namespace Upstream
} // namespace Envoy
