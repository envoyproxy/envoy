#include "common/http/alternate_protocols_cache_impl.h"

namespace Envoy {
namespace Http {

AlternateProtocolsCacheImpl::AlternateProtocolsCacheImpl(TimeSource& time_source)
    : time_source_(time_source) {}

AlternateProtocolsCacheImpl::~AlternateProtocolsCacheImpl() = default;

void AlternateProtocolsCacheImpl::setAlternatives(const Origin& origin,
                                                  const std::vector<AlternateProtocol>& protocols) {
  protocols_[origin] = protocols;
}

OptRef<const std::vector<AlternateProtocolsCache::AlternateProtocol>>
AlternateProtocolsCacheImpl::findAlternatives(const Origin& origin) {
  auto entry_it = protocols_.find(origin);
  if (entry_it == protocols_.end()) {
    return makeOptRefFromPtr<const std::vector<AlternateProtocolsCache::AlternateProtocol>>(
        nullptr);
  }

  auto& protocols = entry_it->second;

  const MonotonicTime now = time_source_.monotonicTime();
  auto it = protocols.begin();
  while (it != protocols.end()) {
    if (now > it->expiration_) {
      it = protocols.erase(it);
      continue;
    }
    ++it;
  }

  if (protocols.empty()) {
    protocols_.erase(entry_it);
    return makeOptRefFromPtr<const std::vector<AlternateProtocolsCache::AlternateProtocol>>(
        nullptr);
  }

  const auto& p = entry_it->second;
  return makeOptRef(p);
}

size_t AlternateProtocolsCacheImpl::size() const { return protocols_.size(); }

} // namespace Http
} // namespace Envoy
