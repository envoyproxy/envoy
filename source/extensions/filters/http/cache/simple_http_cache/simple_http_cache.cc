#include "extensions/filters/http/cache/simple_http_cache/simple_http_cache.h"

#include "envoy/registry/registry.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"

#include "source/extensions/filters/http/cache/simple_http_cache/config.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

class SimpleLookupContext : public LookupContext {
public:
  SimpleLookupContext(SimpleHttpCache& cache, LookupRequest&& request)
      : cache_(cache), request_(std::move(request)) {}

  void getHeaders(LookupHeadersCallback&& cb) override {
    auto entry = cache_.lookup(request_);
    body_ = std::move(entry.body_);
    cb(entry.response_headers_
           ? request_.makeLookupResult(std::move(entry.response_headers_), body_.size())
           : LookupResult{});
  }

  void getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) override {
    ASSERT(range.end() <= body_.length(), "Attempt to read past end of body.");
    cb(std::make_unique<Buffer::OwnedImpl>(&body_[range.begin()], range.length()));
  }

  void getTrailers(LookupTrailersCallback&&) override {
    // TODO(toddmgreer): Support trailers.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  const LookupRequest& request() const { return request_; }

private:
  SimpleHttpCache& cache_;
  const LookupRequest request_;
  std::string body_;
};

class SimpleInsertContext : public InsertContext {
public:
  SimpleInsertContext(LookupContext& lookup_context, SimpleHttpCache& cache)
      : key_(dynamic_cast<SimpleLookupContext&>(lookup_context).request().key()), cache_(cache) {}

  void insertHeaders(const Http::ResponseHeaderMap& response_headers, bool end_stream) override {
    ASSERT(!committed_);
    response_headers_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers);
    if (end_stream) {
      commit();
    }
  }

  void insertBody(const Buffer::Instance& chunk, InsertCallback ready_for_next_chunk,
                  bool end_stream) override {
    ASSERT(!committed_);
    ASSERT(ready_for_next_chunk || end_stream);

    body_.add(chunk);
    if (end_stream) {
      commit();
    } else {
      ready_for_next_chunk(true);
    }
  }

  void insertTrailers(const Http::ResponseTrailerMap&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE; // TODO(toddmgreer): support trailers
  }

private:
  void commit() {
    committed_ = true;
    cache_.insert(key_, std::move(response_headers_), body_.toString());
  }

  Key key_;
  Http::ResponseHeaderMapPtr response_headers_;
  SimpleHttpCache& cache_;
  Buffer::OwnedImpl body_;
  bool committed_ = false;
};
} // namespace

LookupContextPtr SimpleHttpCache::makeLookupContext(LookupRequest&& request) {
  return std::make_unique<SimpleLookupContext>(*this, std::move(request));
}

void SimpleHttpCache::updateHeaders(LookupContextPtr&& lookup_context,
                                    Http::ResponseHeaderMapPtr&& response_headers) {
  ASSERT(lookup_context);
  ASSERT(response_headers);
  // TODO(toddmgreer): Support updating headers.
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

SimpleHttpCache::Entry SimpleHttpCache::lookup(const LookupRequest& request) {
  absl::ReaderMutexLock lock(&mutex_);
  auto iter = map_.find(request.key());
  if (iter == map_.end()) {
    return Entry{};
  }
  ASSERT(iter->second.response_headers_);
  return SimpleHttpCache::Entry{
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*iter->second.response_headers_),
      iter->second.body_};
}

void SimpleHttpCache::insert(const Key& key, Http::ResponseHeaderMapPtr&& response_headers,
                             std::string&& body) {
  absl::WriterMutexLock lock(&mutex_);
  map_[key] = SimpleHttpCache::Entry{std::move(response_headers), std::move(body)};
}

InsertContextPtr SimpleHttpCache::makeInsertContext(LookupContextPtr&& lookup_context) {
  ASSERT(lookup_context != nullptr);
  return std::make_unique<SimpleInsertContext>(*lookup_context, *this);
}

constexpr absl::string_view Name = "envoy.extensions.http.cache.simple";

CacheInfo SimpleHttpCache::cacheInfo() const {
  CacheInfo cache_info;
  cache_info.name_ = Name;
  return cache_info;
}

class SimpleHttpCacheFactory : public HttpCacheFactory {
public:
  // From UntypedFactory
  std::string name() const override { return std::string(Name); }
  // From TypedFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::source::extensions::filters::http::cache::SimpleHttpCacheConfig>();
  }
  // From HttpCacheFactory
  HttpCache&
  getCache(const envoy::extensions::filters::http::cache::v3alpha::CacheConfig&) override {
    return cache_;
  }

private:
  SimpleHttpCache cache_;
};

static Registry::RegisterFactory<SimpleHttpCacheFactory, HttpCacheFactory> register_;

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
