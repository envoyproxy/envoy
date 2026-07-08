#pragma once

#include <memory>

#include "envoy/config/typed_config.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/tracer.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/ext_authz/ext_authz.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

struct RequestAttributes {
  const Http::RequestHeaderMap& headers_;
  Protobuf::Map<std::string, std::string> context_extensions_;
  envoy::config::core::v3::Metadata metadata_context_;
  envoy::config::core::v3::Metadata route_metadata_context_;
};

class AuthCacheSession {
public:
  virtual ~AuthCacheSession() = default;

  class LookupRequest {
  public:
    virtual ~LookupRequest() = default;
    virtual void cancel() PURE;
  };

  using LookupCallback = std::function<void(Filters::Common::ExtAuthz::ResponsePtr&&)>;

  /**
   * Looks for a matching request/response pair in the cache.
   * Can be called at most 1 time per session.
   * If lookup fails or misses, the callback should be invoked with nullptr.
   * Lifetimes of the arguments passed to it must last until cb or LookupRequest::cancel is called.
   * @param decoder_callbacks The stream decoder filter callbacks.
   * @param attributes The RequestAttributes containing authorization context.
   * @param cb The callback to invoke when the lookup completes.
   * @return A LookupRequest handle if the lookup is asynchronous and can be cancelled,
   *         or nullptr if the lookup completed synchronously.
   *         Invalidated when `cb` or `LookupRequest::cancel` is called.
   */
  virtual LookupRequest* lookup(Http::StreamDecoderFilterCallbacks& decoder_callbacks,
                                const RequestAttributes& attributes, LookupCallback&& cb) PURE;

  /**
   * Inserts a response into the cache.
   * Can be called at most 1 time per session. Will only be called if `lookup`
   * does not find an entry (i.e., on cache miss). Uses the key created in `lookup`.
   * @param response The Response received from the authz service.
   */
  virtual void insert(const Filters::Common::ExtAuthz::Response& response) PURE;
};

using AuthCacheSessionPtr = std::unique_ptr<AuthCacheSession>;

class AuthCache {
public:
  virtual ~AuthCache() = default;

  /**
   * Creates a new cache session for a stream filter.
   */
  virtual AuthCacheSessionPtr createSession() PURE;
};

using AuthCachePtr = std::unique_ptr<AuthCache>;

class AuthCacheFactory : public Config::TypedFactory {
public:
  ~AuthCacheFactory() override = default;

  virtual AuthCachePtr createAuthCache(const Protobuf::Message& config,
                                       Server::Configuration::ServerFactoryContext& context) PURE;
  std::string category() const override { return "envoy.filters.http.ext_authz.cache"; }
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
