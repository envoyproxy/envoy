#pragma once

#include <memory>
#include <string>

#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

// Persistent cross-priority host map backing for `MainPrioritySetImpl`. This wraps an `immer`
// persistent map so a membership delta produces a new map that structurally shares unchanged nodes
// with the prior map, making updates O(delta) rather than the O(N) copy the flat HostMap requires.
//
// All `immer` usage is confined to persistent_host_map.cc so the `immer` headers stay out of this
// widely-included header, and the `pimpl` below keeps the `immer` type out of the public interface.
//
// NOTE: This relies on `immer` past the v0.9.1 release, which under-allocated the empty map node
// and tripped the vptr sanitizer.
class PersistentCrossPriorityHostMap {
public:
  PersistentCrossPriorityHostMap();
  ~PersistentCrossPriorityHostMap();

  // Replaces the persistent map with the contents of `flat_map`. Used when a cluster switches from
  // the flat backing to the persistent backing so the accumulated membership carries across.
  void seedFrom(const HostMap& flat_map);

  // Copies the persistent map into `flat_map`. Used when a cluster switches back to the flat
  // backing so the accumulated membership carries across.
  void exportTo(HostMap& flat_map) const;

  // Returns the host stored for `address`, or nullptr if absent.
  HostSharedPtr find(const std::string& address) const;

  // Inserts or replaces the entry for `address`.
  void set(const std::string& address, const HostSharedPtr& host);

  // Removes any entry for `address`.
  void erase(const std::string& address);

  // Publishes the current map as a read-only lookup table snapshot. The snapshot is unaffected by
  // later mutations because the persistent map shares structure across versions.
  HostLookupTableConstSharedPtr publish() const;

private:
  // Holds the `immer` map. Defined in persistent_host_map.cc so the `immer` type stays out of this
  // header.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace Upstream
} // namespace Envoy
