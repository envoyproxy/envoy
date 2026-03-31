#pragma once

#include "envoy/common/hashable.h"
#include "source/common/router/string_accessor_impl.h"

#include <optional>
#include <string_view>

namespace Istio {
namespace Common {

class HashableString : public Envoy::Router::StringAccessorImpl, public Envoy::Hashable {
public:
  HashableString(std::string_view value);

  // Hashable
  std::optional<uint64_t> hash() const override;
};

} // namespace Common
} // namespace Istio
