#pragma once

#include <optional>

#include "envoy/common/hashable.h"

#include "source/common/router/string_accessor_impl.h"

// NOLINT(namespace-envoy)
namespace Istio {
namespace Common {

class HashableString : public Envoy::Router::StringAccessorImpl, public Envoy::Hashable {
public:
  HashableString(absl::string_view value);

  // Hashable
  std::optional<uint64_t> hash() const override;
};

} // namespace Common
} // namespace Istio
