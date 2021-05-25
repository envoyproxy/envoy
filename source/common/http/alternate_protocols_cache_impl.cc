#include "common/http/alternate_protocols_cache_impl.h"

namespace Envoy {
namespace Http {

AlternateProtocolsCacheImpl::AlternateProtocolsCacheImpl(TimeSource& time_source)
    : time_source_(time_source) {}

AlternateProtocolsCacheImpl::~AlternateProtocolsCacheImpl() = default;

void AlternateProtocolsCacheImpl::setAlternatives(const Origin& origin,
                                                  const std::vector<AlternateProtocol>& protocols) {
  // TODO(RyanTheOptimist): Figure out the best place to enforce this limit.
  ASSERT(protocols.size() < 10);
  protocols_[origin] = protocols;
}

OptRef<const std::vector<AlternateProtocolsCache::AlternateProtocol>>
AlternateProtocolsCacheImpl::findAlternatives(const Origin& origin) {
  auto entry_it = protocols_.find(origin);
  if (entry_it == protocols_.end()) {
    return makeOptRefFromPtr<const std::vector<AlternateProtocol>>(nullptr);
  }

  std::vector<AlternateProtocol>& protocols = entry_it->second;

  const MonotonicTime now = time_source_.monotonicTime();
  protocols.erase(std::remove_if(protocols.begin(), protocols.end(),
                                 [now](const AlternateProtocol& protocol) {
                                   return (now > protocol.expiration_);
                                 }),
                  protocols.end());

  if (protocols.empty()) {
    protocols_.erase(entry_it);
    return makeOptRefFromPtr<const std::vector<AlternateProtocol>>(nullptr);
  }

  return makeOptRef(const_cast<const std::vector<AlternateProtocol>&>(protocols));
}

size_t AlternateProtocolsCacheImpl::size() const { return protocols_.size(); }

} // namespace Http
} // namespace Envoy
