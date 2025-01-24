//#include "envoy/thread_local/thread_local.h"
#include "source/extensions/filters/http/cache/http_cache.h"

#include "envoy/extensions/http/cache/redis_http_cache/v3/redis_http_cache.pb.h"
#include "envoy/extensions/http/cache/redis_http_cache/v3/redis_http_cache.pb.validate.h"
//#include "source/extensions/common/redis/async_redis_client_impl.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_client.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

class RedisHttpCacheLookupContext : public LookupContext {
public:
  RedisHttpCacheLookupContext(Event::Dispatcher& dispatcher, 
//   RedisHttpCache& cache, 
                    Upstream::ClusterManager& cluster_manager,
                                      ThreadLocal::TypedSlot</*RedisHttpCache::*/ThreadLocalRedisClient>& tls_slot,
                    LookupRequest&& lookup)
      : dispatcher_(dispatcher), 
    // cache_(cache), 
    key_(lookup.key()), tls_slot_(tls_slot), lookup_(std::move(lookup)),
        /*redis_client_(redis_client),*/ cluster_manager_(cluster_manager) {}

  // From LookupContext
  void getHeaders(LookupHeadersCallback&&/* cb*/) final;// {ASSERT(false);}
  void getBody(const AdjustedByteRange& /*range*/, LookupBodyCallback&&/* cb*/) final; // {ASSERT(false);}
  void getTrailers(LookupTrailersCallback&&/* cb*/) final {ASSERT(false);}
  void onDestroy() final {/*ASSERT(false);*/}
  // This shouldn't be necessary since onDestroy is supposed to always be called, but in some
  // tests it is not.
  ~RedisHttpCacheLookupContext() override {/* onDestroy();*/ }

  const LookupRequest& lookup() const { return lookup_; }
  const Key& key() const { return key_; }
  //bool workInProgress() const;
  Event::Dispatcher* dispatcher() const { return &dispatcher_; }

  LookupHeadersCallback cb_;
  LookupBodyCallback cb1_;

private:
  void doCacheMiss();
  void doCacheEntryInvalid();
  void getHeaderBlockFromFile();
  void getHeadersFromFile();
  void closeFileAndGetHeadersAgainWithNewVaryKey();

  // In the event that the cache failed to retrieve, remove the cache entry from the
  // cache so we don't keep repeating the same failure.
  void invalidateCacheEntry();

  Event::Dispatcher& dispatcher_;

  // Cache defines which cluster to use.
 // RedisHttpCache& cache_;

  Key key_;

  LookupHeadersCallback lookup_headers_callback_;
  ThreadLocal::TypedSlot</*RedisHttpCache::*/ThreadLocalRedisClient>& tls_slot_;
  const LookupRequest lookup_;
  //Extensions::Common::Redis::RedisAsyncClient& redis_client_;
  Upstream::ClusterManager& cluster_manager_; 
};

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
