#pragma once

#include "envoy/common/hashable.h"

#include "source/common/router/string_accessor_impl.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Istio { // NOLINT(namespace-envoy)
namespace Common {

class HashableString : public Envoy::Router::StringAccessorImpl, public Envoy::Hashable {
public:
  HashableString(absl::string_view value);

  // Hashable
  absl::optional<uint64_t> hash() const override;
};

} // namespace Common
} // namespace Istio
