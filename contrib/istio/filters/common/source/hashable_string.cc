#include "contrib/istio/filters/common/source/hashable_string.h"

#include <memory>
#include <string>

#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/hash.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Istio { // NOLINT(namespace-envoy)
namespace Common {

HashableString::HashableString(absl::string_view value)
    : Envoy::Router::StringAccessorImpl(value) {}

absl::optional<uint64_t> HashableString::hash() const {
  return Envoy::HashUtil::xxHash64(asString());
}

namespace {

class HashableStringObjectFactory : public Envoy::StreamInfo::FilterState::ObjectFactory {
public:
  // ObjectFactory
  std::string name() const override { return "istio.hashable_string"; }

  std::unique_ptr<Envoy::StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<HashableString>(data);
  }
};

REGISTER_FACTORY(HashableStringObjectFactory, Envoy::StreamInfo::FilterState::ObjectFactory);

} // namespace

} // namespace Common
} // namespace Istio
