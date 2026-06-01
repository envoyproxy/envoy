#pragma once

#include <memory>

#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/tracer.h"

#include "source/extensions/filters/common/ext_authz/ext_authz.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

class AuthCache {
public:
  virtual ~AuthCache() = default;

  using LookupCallback = std::function<void(Filters::Common::ExtAuthz::ResponsePtr&&)>;

  /**
   * Looks for a matching request/response pair in the cache.
   * If lookup fails or misses, the callback should be invoked with nullptr.
   * @param request The CheckRequest containing authorization context.
   * @param cb The callback to invoke when the lookup completes. Implementations may either call
   * `cb` synchronously or asynchronously (via Dispatcher::post).
   * @param parent_span The parent span for tracing.
   * @param stream_info The stream info.
   */
  virtual void lookup(const envoy::service::auth::v3::CheckRequest& request, LookupCallback&& cb,
                      Tracing::Span& parent_span, const StreamInfo::StreamInfo& stream_info) = 0;

  /**
   * Inserts a response into the cache.
   * @param response The Response received from the authz service.
   * @param parent_span The parent span for tracing.
   * @param stream_info The stream info.
   */
  virtual void insert(const Filters::Common::ExtAuthz::Response& response,
                      Tracing::Span& parent_span, const StreamInfo::StreamInfo& stream_info) = 0;

  /**
   * Called when the filter is being destroyed. The cache implementation must
   * abort any in-progress asynchronous operations and ensure LookupCallback is
   * never called after this.
   */
  virtual void onDestroy() = 0;
};

using AuthCachePtr = std::unique_ptr<AuthCache>;

class AuthCacheFactory : public Config::TypedFactory {
public:
  virtual AuthCachePtr createAuthCache(const Protobuf::Message& config,
                                       Server::Configuration::ServerFactoryContext& context) = 0;
  std::string category() const override { return "envoy.filters.http.ext_authz.cache"; }
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
