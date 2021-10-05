#include "source/common/http/alternate_protocols_cache_impl.h"

#include "source/common/common/logger.h"

#include "quiche/spdy/core/spdy_alt_svc_wire_format.h"
#include "re2/re2.h"

namespace Envoy {
namespace Http {
namespace {

struct RegexHolder {
  RegexHolder() : origin_regex("(.*)://(.*):(\\d+)") {}

  const re2::RE2 origin_regex;
};

using ConstRegexHolder = ConstSingleton<RegexHolder>;

} // namespace

std::string
AlternateProtocolsCacheImpl::originToString(const AlternateProtocolsCache::Origin& origin) {
  return absl::StrCat(origin.scheme_, "://", origin.hostname_, ":", origin.port_);
}

absl::optional<AlternateProtocolsCache::Origin>
AlternateProtocolsCacheImpl::stringToOrigin(const std::string& str) {
  const re2::RE2& origin_regex = ConstRegexHolder::get().origin_regex;
  std::string scheme;
  std::string hostname;
  int port = 0;
  if (re2::RE2::FullMatch(str.c_str(), origin_regex, &scheme, &hostname, &port)) {
    return AlternateProtocolsCache::Origin(scheme, hostname, port);
  }
  return {};
}

std::string AlternateProtocolsCacheImpl::protocolsToStringForCache(
    const std::vector<AlternateProtocol>& protocols, TimeSource& /*time_source*/) {
  if (protocols.empty()) {
    return std::string("clear");
  }
  std::string value;
  for (auto& protocol : protocols) {
    if (!value.empty()) {
      value.push_back(',');
    }
    absl::StrAppend(&value, protocol.alpn_, "=\"", protocol.hostname_, ":", protocol.port_, "\"");

    // Note this is _not_ actually the max age, but the absolute time at which
    // this entry will expire. protocolsFromString will convert back to ma.
    absl::StrAppend(
        &value, "; ma=",
        std::chrono::duration_cast<std::chrono::seconds>(protocol.expiration_.time_since_epoch())
            .count());
  }
  return value;
}

absl::optional<std::vector<AlternateProtocolsCache::AlternateProtocol>>
AlternateProtocolsCacheImpl::protocolsFromString(absl::string_view alt_svc_string,
                                                 TimeSource& time_source, bool from_cache) {
  std::vector<AlternateProtocol> protocols;
  spdy::SpdyAltSvcWireFormat::AlternativeServiceVector altsvc_vector;
  if (!spdy::SpdyAltSvcWireFormat::ParseHeaderFieldValue(alt_svc_string, &altsvc_vector)) {
    return {};
  }
  for (const auto& alt_svc : altsvc_vector) {
    MonotonicTime expiration;
    if (from_cache) {
      auto expire_time_from_epoch = std::chrono::seconds(alt_svc.max_age_seconds);
      auto time_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(
          time_source.monotonicTime().time_since_epoch());
      if (expire_time_from_epoch < time_since_epoch) {
        expiration = time_source.monotonicTime();
      } else {
        expiration = time_source.monotonicTime() + (expire_time_from_epoch - time_since_epoch);
      }
    } else {
      expiration = time_source.monotonicTime() + std::chrono::seconds(alt_svc.max_age_seconds);
    }
    Http::AlternateProtocolsCache::AlternateProtocol protocol(alt_svc.protocol_id, alt_svc.host,
                                                              alt_svc.port, expiration);
    protocols.push_back(protocol);
  }
  return protocols;
}

AlternateProtocolsCacheImpl::AlternateProtocolsCacheImpl(
    TimeSource& time_source, std::unique_ptr<KeyValueStore>&& key_value_store, size_t max_entries)
    : time_source_(time_source), key_value_store_(std::move(key_value_store)),
      max_entries_(max_entries > 0 ? max_entries : 1024) {
  if (key_value_store_) {
    KeyValueStore::ConstIterateCb load = [this](const std::string& key, const std::string& value) {
      absl::optional<std::vector<AlternateProtocolsCache::AlternateProtocol>> protocols =
          protocolsFromString(value, time_source_, true);
      absl::optional<Origin> origin = stringToOrigin(key);
      if (protocols.has_value() && origin.has_value()) {
        setAlternativesImpl(origin.value(), protocols.value());
      } else {
        ENVOY_LOG(warn,
                  fmt::format("Unable to parse cache entry with key: {} value: {}", key, value));
      }
      return KeyValueStore::Iterate::Continue;
    };
    key_value_store_->iterate(load);
  }
}

AlternateProtocolsCacheImpl::~AlternateProtocolsCacheImpl() = default;

void AlternateProtocolsCacheImpl::setAlternatives(const Origin& origin,
                                                  std::vector<AlternateProtocol>& protocols) {
  setAlternativesImpl(origin, protocols);
  if (key_value_store_) {
    key_value_store_->addOrUpdate(originToString(origin),
                                  protocolsToStringForCache(protocols, time_source_));
  }
}

void AlternateProtocolsCacheImpl::setAlternativesImpl(const Origin& origin,
                                                      std::vector<AlternateProtocol>& protocols) {
  static const size_t max_protocols = 10;
  if (protocols.size() > max_protocols) {
    ENVOY_LOG_MISC(trace, "Too many alternate protocols: {}, truncating", protocols.size());
    protocols.erase(protocols.begin() + max_protocols, protocols.end());
  }
  while (protocols_.size() >= max_entries_) {
    auto iter = protocols_.begin();
    key_value_store_->remove(originToString(iter->first));
    protocols_.erase(iter);
  }
  protocols_[origin] = protocols;
}

OptRef<const std::vector<AlternateProtocolsCache::AlternateProtocol>>
AlternateProtocolsCacheImpl::findAlternatives(const Origin& origin) {
  auto entry_it = protocols_.find(origin);
  if (entry_it == protocols_.end()) {
    return makeOptRefFromPtr<const std::vector<AlternateProtocol>>(nullptr);
  }
  std::vector<AlternateProtocol>& protocols = entry_it->second;

  auto original_size = protocols.size();
  const MonotonicTime now = time_source_.monotonicTime();
  protocols.erase(std::remove_if(protocols.begin(), protocols.end(),
                                 [now](const AlternateProtocol& protocol) {
                                   return (now > protocol.expiration_);
                                 }),
                  protocols.end());

  if (protocols.empty()) {
    protocols_.erase(entry_it);
    if (key_value_store_) {
      key_value_store_->remove(originToString(origin));
    }
    return makeOptRefFromPtr<const std::vector<AlternateProtocol>>(nullptr);
  }
  if (key_value_store_ && original_size != protocols.size()) {
    key_value_store_->addOrUpdate(originToString(origin),
                                  protocolsToStringForCache(protocols, time_source_));
  }

  return makeOptRef(const_cast<const std::vector<AlternateProtocol>&>(protocols));
}

size_t AlternateProtocolsCacheImpl::size() const { return protocols_.size(); }

} // namespace Http
} // namespace Envoy
