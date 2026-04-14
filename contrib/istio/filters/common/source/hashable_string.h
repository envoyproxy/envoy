#pragma once

#include "envoy/common/hashable.h"

#include "source/common/router/string_accessor_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Istio {
namespace Common {

class HashableString : public Router::StringAccessorImpl, public Hashable {
public:
  HashableString(absl::string_view value);

  // Hashable
  absl::optional<uint64_t> hash() const override;
};

} // namespace Common
} // namespace Istio
} // namespace Envoy
