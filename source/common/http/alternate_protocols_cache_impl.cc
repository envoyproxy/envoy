#include "source/common/http/alternate_protocols_cache_impl.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Http {

AlternateProtocolsCacheImpl::AlternateProtocolsCacheImpl(TimeSource& time_source)
    : time_source_(time_source) {}

AlternateProtocolsCacheImpl::~AlternateProtocolsCacheImpl() = default;

void AlternateProtocolsCacheImpl::setAlternatives(const Origin& origin,
                                                  const std::vector<AlternateProtocol>& protocols) {
  protocols_[origin] = protocols;
  static const size_t max_protocols = 10;
  if (protocols.size() > max_protocols) {
    ENVOY_LOG_MISC(trace, "Too many alternate protocols: {}, truncating", protocols.size());
    std::vector<AlternateProtocol>& p = protocols_[origin];
    p.erase(p.begin() + max_protocols, p.end());
  }
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
