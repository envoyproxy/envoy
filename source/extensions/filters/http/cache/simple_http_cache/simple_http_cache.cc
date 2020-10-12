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
    cb(entry.response_headers_ ? request_.makeLookupResult(std::move(entry.response_headers_),
                                                           std::move(entry.metadata_), body_.size())
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
  void onDestroy() override {}

private:
  SimpleHttpCache& cache_;
  const LookupRequest request_;
  std::string body_;
};

class SimpleInsertContext : public InsertContext {
public:
  SimpleInsertContext(LookupContext& lookup_context, SimpleHttpCache& cache)
      : key_(dynamic_cast<SimpleLookupContext&>(lookup_context).request().key()),
        entry_vary_headers_(
            dynamic_cast<SimpleLookupContext&>(lookup_context).request().getVaryHeaders()),
        cache_(cache) {}

  void insertHeaders(const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata, bool end_stream) override {
    ASSERT(!committed_);
    response_headers_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers);
    metadata_ = metadata;
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

  void onDestroy() override {}

private:
  void commit() {
    committed_ = true;
    if (VaryHeader::hasVary(*response_headers_)) {
      cache_.varyInsert(key_, std::move(response_headers_), std::move(metadata_), body_.toString(),
                        entry_vary_headers_);
    } else {
      cache_.insert(key_, std::move(response_headers_), std::move(metadata_), body_.toString());
    }
  }

  Key key_;
  Http::ResponseHeaderMapPtr response_headers_;
  ResponseMetadata metadata_;
  const Http::RequestHeaderMap& entry_vary_headers_;
  SimpleHttpCache& cache_;
  Buffer::OwnedImpl body_;
  bool committed_ = false;
};
} // namespace

LookupContextPtr SimpleHttpCache::makeLookupContext(LookupRequest&& request) {
  return std::make_unique<SimpleLookupContext>(*this, std::move(request));
}

void SimpleHttpCache::updateHeaders(const LookupContext&, const Http::ResponseHeaderMap&,
                                    const ResponseMetadata&) {
  // TODO(toddmgreer): Support updating headers.
  // Not implemented yet, however this is called during tests
  // NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

SimpleHttpCache::Entry SimpleHttpCache::lookup(const LookupRequest& request) {
  absl::ReaderMutexLock lock(&mutex_);
  auto iter = map_.find(request.key());
  if (iter == map_.end()) {
    return Entry{};
  }
  ASSERT(iter->second.response_headers_);

  if (VaryHeader::hasVary(*iter->second.response_headers_)) {
    return varyLookup(request, iter->second.response_headers_);
  } else {
    return SimpleHttpCache::Entry{
        Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*iter->second.response_headers_),
        iter->second.metadata_, iter->second.body_};
  }
}

void SimpleHttpCache::insert(const Key& key, Http::ResponseHeaderMapPtr&& response_headers,
                             ResponseMetadata&& metadata, std::string&& body) {
  absl::WriterMutexLock lock(&mutex_);
  map_[key] =
      SimpleHttpCache::Entry{std::move(response_headers), std::move(metadata), std::move(body)};
}

SimpleHttpCache::Entry
SimpleHttpCache::varyLookup(const LookupRequest& request,
                            const Http::ResponseHeaderMapPtr& response_headers) {
  // This method should be called from lookup, which holds the mutex for reading.
  mutex_.AssertReaderHeld();

  const auto vary_header = response_headers->get(Http::Headers::get().Vary);
  ASSERT(!vary_header.empty());

  Key varied_request_key = request.key();
  const std::string vary_key = VaryHeader::createVaryKey(vary_header, request.getVaryHeaders());
  varied_request_key.add_custom_fields(vary_key);

  auto iter = map_.find(varied_request_key);
  if (iter == map_.end()) {
    return SimpleHttpCache::Entry{};
  }
  ASSERT(iter->second.response_headers_);

  return SimpleHttpCache::Entry{
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*iter->second.response_headers_),
      iter->second.metadata_, iter->second.body_};
}

void SimpleHttpCache::varyInsert(const Key& request_key,
                                 Http::ResponseHeaderMapPtr&& response_headers,
                                 ResponseMetadata&& metadata, std::string&& body,
                                 const Http::RequestHeaderMap& request_vary_headers) {
  absl::WriterMutexLock lock(&mutex_);

  const auto vary_header = response_headers->get(Http::Headers::get().Vary);
  ASSERT(!vary_header.empty());

  // Insert the varied response.
  Key varied_request_key = request_key;
  const std::string vary_key = VaryHeader::createVaryKey(vary_header, request_vary_headers);
  varied_request_key.add_custom_fields(vary_key);
  map_[varied_request_key] =
      SimpleHttpCache::Entry{std::move(response_headers), std::move(metadata), std::move(body)};

  // Add a special entry to flag that this request generates varied responses.
  auto iter = map_.find(request_key);
  if (iter == map_.end()) {
    Http::ResponseHeaderMapPtr vary_only_map =
        Http::createHeaderMap<Http::ResponseHeaderMapImpl>({});
    // TODO(mattklein123): Support multiple vary headers and/or just make the vary header inline.
    vary_only_map->setCopy(Http::Headers::get().Vary, vary_header[0]->value().getStringView());
    // TODO(cbdm): In a cache that evicts entries, we could maintain a list of the "varykey"s that
    // we have inserted as the body for this first lookup. This way, we would know which keys we
    // have inserted for that resource. For the first entry simply use vary_key as the entry_list,
    // for future entries append vary_key to existing list.
    std::string entry_list;
    map_[request_key] = SimpleHttpCache::Entry{std::move(vary_only_map), {}, std::move(entry_list)};
  }
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
