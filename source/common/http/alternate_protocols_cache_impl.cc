#include "common/http/alternate_protocols_cache_impl.h"

namespace Envoy {
namespace Http {

AlternateProtocolsCacheImpl::AlternateProtocol::AlternateProtocol(absl::string_view alpn,
                                                                  absl::string_view hostname,
                                                                  uint32_t port)
    : alpn_(alpn), hostname_(hostname), port_(port) {}

AlternateProtocolsCacheImpl::Origin::Origin(absl::string_view scheme, absl::string_view hostname,
                                            uint32_t port)
    : scheme_(scheme), hostname_(hostname), port_(port) {}

AlternateProtocolsCacheImpl::AlternateProtocolsCacheImpl(TimeSource& time_source)
    : time_source_(time_source) {}

AlternateProtocolsCacheImpl::~AlternateProtocolsCacheImpl() = default;

void AlternateProtocolsCacheImpl::setAlternatives(const Origin& origin,
                                                  const std::vector<AlternateProtocol>& protocols,
                                                  const MonotonicTime& expiration) {
  Entry& entry = protocols_[origin];
  if (entry.protocols_ != protocols) {
    entry.protocols_ = protocols;
  }
  if (entry.expiration_ != expiration) {
    entry.expiration_ = expiration;
  }
}

OptRef<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>>
AlternateProtocolsCacheImpl::findAlternatives(const Origin& origin) {
  auto entry_it = protocols_.find(origin);
  if (entry_it == protocols_.end()) {
    return makeOptRefFromPtr<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>>(
        nullptr);
  }

  const Entry& entry = entry_it->second;
  if (time_source_.monotonicTime() > entry.expiration_) {
    // Expire the entry.
    protocols_.erase(entry_it);
    return makeOptRefFromPtr<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>>(
        nullptr);
  }
  return makeOptRef(entry.protocols_);
}

size_t AlternateProtocolsCacheImpl::size() const { return protocols_.size(); }

} // namespace Http
} // namespace Envoy
