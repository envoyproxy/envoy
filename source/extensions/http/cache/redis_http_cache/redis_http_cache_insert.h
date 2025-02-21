#pragma once

#include "source/extensions/http/cache/redis_http_cache/redis_http_cache.h"
#include "source/extensions/http/cache/redis_http_cache/cache_header_proto_util.h"

//#include "source/extensions/filters/http/cache/cache_custom_headers.h"
//#include "source/extensions/http/cache/redis_http_cache/cache_header_proto_util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

class RedisHttpCacheInsertContext : public InsertContext, public Logger::Loggable<Logger::Id::cache_filter> {
public:
  RedisHttpCacheInsertContext(std::unique_ptr<RedisHttpCacheLookupContext> lookup_context, 
ThreadLocal::TypedSlot</*RedisHttpCache::*/ThreadLocalRedisClient>& tls) 
    : lookup_(std::move(lookup_context)), tls_slot_(tls)  {
    alive_ = std::make_shared<bool>(true);
}
  void insertHeaders(const Http::ResponseHeaderMap& /*response_headers*/,
                     const ResponseMetadata& /*metadata*/, InsertCallback /*insert_complete*/,
                     bool /*end_stream*/) override;
  void insertBody(const Buffer::Instance& /*chunk*/, InsertCallback ready_for_next_chunk,
                  bool /*end_stream*/) override; // {ASSERT(false); ready_for_next_chunk(true);}
  void insertTrailers(const Http::ResponseTrailerMap& /*trailers*/,
                      InsertCallback/* insert_complete*/) override;
  void onDestroy() override {/*ASSERT(false);*/}
  void onStreamEnd();

private:
  // Event::Dispatcher* dispatcher() const;
  // The sequence of actions involved in writing the cache entry to a file. Each
    //LookupContextPtr lookup_;
    std::unique_ptr<RedisHttpCacheLookupContext> lookup_;
    InsertCallback insert_callback_;
    // TODO: can I move cluster_manager to struct stored in threadlocal.
//                    Upstream::ClusterManager& cluster_manager_;
                                      ThreadLocal::TypedSlot</*RedisHttpCache::*/ThreadLocalRedisClient>& tls_slot_;
    bool first_body_chunk_{true};
    uint64_t body_length_{0};
  CacheFileHeader header_proto_;
 //InsertCallback cb1_;

    // This is used to derive weak pointer gived to lookup and insert contexts.
    // Callbacks in those contexts check if the weak pointer is still valid.
    // If the weak pointer is expired, it means that the session which issued the
    // call to redis has been closed and the associated cache filter has been 
    // destroyed.
  std::shared_ptr<bool> alive_;

};

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
