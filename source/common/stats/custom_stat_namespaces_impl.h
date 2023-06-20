#pragma once

#include "envoy/stats/custom_stat_namespaces.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Stats {

class CustomStatNamespacesImpl : public CustomStatNamespaces {
public:
  ~CustomStatNamespacesImpl() override = default;

  // CustomStatNamespaces
  bool registered(const absl::string_view name) const override;
  void registerStatNamespace(const absl::string_view name) override;
  absl::optional<absl::string_view>
  stripRegisteredPrefix(const absl::string_view stat_name) const override;

private:
  absl::flat_hash_set<std::string> namespaces_;
};

} // namespace Stats
} // namespace Envoy
