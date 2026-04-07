#include "contrib/istio/filters/common/source/hashable_string.h"

#include <memory>
#include <string>

#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/hash.h"

namespace Envoy {
namespace Istio {
namespace Common {

HashableString::HashableString(absl::string_view value) : Router::StringAccessorImpl(value) {}

absl::optional<uint64_t> HashableString::hash() const { return HashUtil::xxHash64(asString()); }

namespace {

class HashableStringObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  // ObjectFactory
  std::string name() const override { return "istio.hashable_string"; }

  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<HashableString>(data);
  }
};

REGISTER_FACTORY(HashableStringObjectFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace

} // namespace Common
} // namespace Istio
} // namespace Envoy
