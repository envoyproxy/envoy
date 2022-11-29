#include "source/extensions/http/cache/simple_http_cache/simple_http_cache.h"

#include "envoy/extensions/http/cache/simple_http_cache/v3/config.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

// Returns a Key with the vary header added to custom_fields.
// It is an error to call this with headers that don't include vary.
// Returns nullopt if the vary headers in the response are not
// compatible with the VaryAllowList in the LookupRequest.
absl::optional<Key> variedRequestKey(const LookupRequest& request,
                                     const Http::ResponseHeaderMap& response_headers) {
  absl::btree_set<absl::string_view> vary_header_values =
      VaryHeaderUtils::getVaryValues(response_headers);
  ASSERT(!vary_header_values.empty());
  const absl::optional<std::string> vary_identifier = VaryHeaderUtils::createVaryIdentifier(
      request.varyAllowList(), vary_header_values, request.requestHeaders());
  if (!vary_identifier.has_value()) {
    return absl::nullopt;
  }
  Key varied_request_key = request.key();
  varied_request_key.add_custom_fields(vary_identifier.value());
  return varied_request_key;
}

class SimpleLookupContext : public LookupContext {
public:
  SimpleLookupContext(SimpleHttpCache& cache, LookupRequest&& request)
      : cache_(cache), request_(std::move(request)) {}

  void getHeaders(LookupHeadersCallback&& cb) override {
    auto entry = cache_.lookup(request_);
    body_ = std::move(entry.body_);
    trailers_ = std::move(entry.trailers_);
    cb(entry.response_headers_ ? request_.makeLookupResult(std::move(entry.response_headers_),
                                                           std::move(entry.metadata_), body_.size(),
                                                           trailers_ != nullptr)
                               : LookupResult{});
  }

  void getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) override {
    ASSERT(range.end() <= body_.length(), "Attempt to read past end of body.");
    cb(std::make_unique<Buffer::OwnedImpl>(&body_[range.begin()], range.length()));
  }

  // The cache must call cb with the cached trailers.
  void getTrailers(LookupTrailersCallback&& cb) override {
    ASSERT(trailers_);
    cb(std::move(trailers_));
  }

  const LookupRequest& request() const { return request_; }
  void onDestroy() override {}

private:
  SimpleHttpCache& cache_;
  const LookupRequest request_;
  std::string body_;
  Http::ResponseTrailerMapPtr trailers_;
};

class SimpleInsertContext : public InsertContext {
public:
  SimpleInsertContext(LookupContext& lookup_context, SimpleHttpCache& cache)
      : key_(dynamic_cast<SimpleLookupContext&>(lookup_context).request().key()),
        request_headers_(
            dynamic_cast<SimpleLookupContext&>(lookup_context).request().requestHeaders()),
        vary_allow_list_(
            dynamic_cast<SimpleLookupContext&>(lookup_context).request().varyAllowList()),
        cache_(cache) {}

  void insertHeaders(const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata, InsertCallback insert_success,
                     bool end_stream) override {
    ASSERT(!committed_);
    response_headers_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers);
    metadata_ = metadata;
    if (end_stream) {
      insert_success(commit());
    } else {
      insert_success(true);
    }
  }

  void insertBody(const Buffer::Instance& chunk, InsertCallback ready_for_next_chunk,
                  bool end_stream) override {
    ASSERT(!committed_);
    ASSERT(ready_for_next_chunk || end_stream);

    body_.add(chunk);
    if (end_stream) {
      ready_for_next_chunk(commit());
    } else {
      ready_for_next_chunk(true);
    }
  }

  void insertTrailers(const Http::ResponseTrailerMap& trailers,
                      InsertCallback insert_complete) override {
    ASSERT(!committed_);
    trailers_ = Http::createHeaderMap<Http::ResponseTrailerMapImpl>(trailers);
    insert_complete(commit());
  }

  void onDestroy() override {}

private:
  bool commit() {
    committed_ = true;
    if (VaryHeaderUtils::hasVary(*response_headers_)) {
      return cache_.varyInsert(key_, std::move(response_headers_), std::move(metadata_),
                               body_.toString(), request_headers_, vary_allow_list_,
                               std::move(trailers_));
    } else {
      return cache_.insert(key_, std::move(response_headers_), std::move(metadata_),
                           body_.toString(), std::move(trailers_));
    }
  }

  Key key_;
  const Http::RequestHeaderMap& request_headers_;
  const VaryAllowList& vary_allow_list_;
  Http::ResponseHeaderMapPtr response_headers_;
  ResponseMetadata metadata_;
  SimpleHttpCache& cache_;
  Buffer::OwnedImpl body_;
  bool committed_ = false;
  Http::ResponseTrailerMapPtr trailers_;
};
} // namespace

LookupContextPtr SimpleHttpCache::makeLookupContext(LookupRequest&& request,
                                                    Http::StreamDecoderFilterCallbacks&) {
  return std::make_unique<SimpleLookupContext>(*this, std::move(request));
}

void SimpleHttpCache::updateHeaders(const LookupContext& lookup_context,
                                    const Http::ResponseHeaderMap& response_headers,
                                    const ResponseMetadata& metadata,
                                    std::function<void(bool)> on_complete) {
  const auto& simple_lookup_context = static_cast<const SimpleLookupContext&>(lookup_context);
  const Key& key = simple_lookup_context.request().key();
  absl::WriterMutexLock lock(&mutex_);
  auto iter = map_.find(key);
  if (iter == map_.end() || !iter->second.response_headers_) {
    on_complete(false);
    return;
  }
  if (VaryHeaderUtils::hasVary(*iter->second.response_headers_)) {
    absl::optional<Key> varied_key =
        variedRequestKey(simple_lookup_context.request(), *iter->second.response_headers_);
    if (!varied_key.has_value()) {
      on_complete(false);
      return;
    }
    iter = map_.find(varied_key.value());
    if (iter == map_.end() || !iter->second.response_headers_) {
      on_complete(false);
      return;
    }
  }
  Entry& entry = iter->second;

  applyHeaderUpdate(response_headers, *entry.response_headers_);
  entry.metadata_ = metadata;
  on_complete(true);
}

SimpleHttpCache::Entry SimpleHttpCache::lookup(const LookupRequest& request) {
  absl::ReaderMutexLock lock(&mutex_);
  auto iter = map_.find(request.key());
  if (iter == map_.end()) {
    return Entry{};
  }
  ASSERT(iter->second.response_headers_);

  if (VaryHeaderUtils::hasVary(*iter->second.response_headers_)) {
    return varyLookup(request, iter->second.response_headers_);
  } else {
    Http::ResponseTrailerMapPtr trailers_map;
    if (iter->second.trailers_) {
      trailers_map = Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*iter->second.trailers_);
    }
    return SimpleHttpCache::Entry{
        Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*iter->second.response_headers_),
        iter->second.metadata_, iter->second.body_, std::move(trailers_map)};
  }
}

bool SimpleHttpCache::insert(const Key& key, Http::ResponseHeaderMapPtr&& response_headers,
                             ResponseMetadata&& metadata, std::string&& body,
                             Http::ResponseTrailerMapPtr&& trailers) {
  absl::WriterMutexLock lock(&mutex_);
  map_[key] = SimpleHttpCache::Entry{std::move(response_headers), std::move(metadata),
                                     std::move(body), std::move(trailers)};
  return true;
}

SimpleHttpCache::Entry
SimpleHttpCache::varyLookup(const LookupRequest& request,
                            const Http::ResponseHeaderMapPtr& response_headers) {
  // This method should be called from lookup, which holds the mutex for reading.
  mutex_.AssertReaderHeld();

  absl::optional<Key> varied_key = variedRequestKey(request, *response_headers);
  if (!varied_key.has_value()) {
    return SimpleHttpCache::Entry{};
  }
  Key& varied_request_key = varied_key.value();

  auto iter = map_.find(varied_request_key);
  if (iter == map_.end()) {
    return SimpleHttpCache::Entry{};
  }
  ASSERT(iter->second.response_headers_);
  Http::ResponseTrailerMapPtr trailers_map;
  if (iter->second.trailers_) {
    trailers_map = Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*iter->second.trailers_);
  }

  return SimpleHttpCache::Entry{
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*iter->second.response_headers_),
      iter->second.metadata_, iter->second.body_, std::move(trailers_map)};
}

bool SimpleHttpCache::varyInsert(const Key& request_key,
                                 Http::ResponseHeaderMapPtr&& response_headers,
                                 ResponseMetadata&& metadata, std::string&& body,
                                 const Http::RequestHeaderMap& request_headers,
                                 const VaryAllowList& vary_allow_list,
                                 Http::ResponseTrailerMapPtr&& trailers) {
  absl::WriterMutexLock lock(&mutex_);

  absl::btree_set<absl::string_view> vary_header_values =
      VaryHeaderUtils::getVaryValues(*response_headers);
  ASSERT(!vary_header_values.empty());

  // Insert the varied response.
  Key varied_request_key = request_key;
  const absl::optional<std::string> vary_identifier =
      VaryHeaderUtils::createVaryIdentifier(vary_allow_list, vary_header_values, request_headers);
  if (!vary_identifier.has_value()) {
    // Skip the insert if we are unable to create a vary key.
    return false;
  }

  varied_request_key.add_custom_fields(vary_identifier.value());
  map_[varied_request_key] = SimpleHttpCache::Entry{
      std::move(response_headers), std::move(metadata), std::move(body), std::move(trailers)};

  // Add a special entry to flag that this request generates varied responses.
  auto iter = map_.find(request_key);
  if (iter == map_.end()) {
    Envoy::Http::ResponseHeaderMapPtr vary_only_map =
        Envoy::Http::createHeaderMap<Envoy::Http::ResponseHeaderMapImpl>({});
    vary_only_map->setCopy(Envoy::Http::CustomHeaders::get().Vary,
                           absl::StrJoin(vary_header_values, ","));
    // TODO(cbdm): In a cache that evicts entries, we could maintain a list of the "varykey"s that
    // we have inserted as the body for this first lookup. This way, we would know which keys we
    // have inserted for that resource. For the first entry simply use vary_identifier as the
    // entry_list; for future entries append vary_identifier to existing list.
    std::string entry_list;
    map_[request_key] =
        SimpleHttpCache::Entry{std::move(vary_only_map), {}, std::move(entry_list), {}};
  }
  return true;
}

InsertContextPtr SimpleHttpCache::makeInsertContext(LookupContextPtr&& lookup_context,
                                                    Http::StreamEncoderFilterCallbacks&) {
  ASSERT(lookup_context != nullptr);
  return std::make_unique<SimpleInsertContext>(*lookup_context, *this);
}

constexpr absl::string_view Name = "envoy.extensions.http.cache.simple";

CacheInfo SimpleHttpCache::cacheInfo() const {
  CacheInfo cache_info;
  cache_info.name_ = Name;
  return cache_info;
}

SINGLETON_MANAGER_REGISTRATION(simple_http_cache_singleton);

class SimpleHttpCacheFactory : public HttpCacheFactory {
public:
  // From UntypedFactory
  std::string name() const override { return std::string(Name); }
  // From TypedFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::http::cache::simple_http_cache::v3::SimpleHttpCacheConfig>();
  }
  // From HttpCacheFactory
  std::shared_ptr<HttpCache>
  getCache(const envoy::extensions::filters::http::cache::v3::CacheConfig&,
           Server::Configuration::FactoryContext& context) override {
    return context.singletonManager().getTyped<SimpleHttpCache>(
        SINGLETON_MANAGER_REGISTERED_NAME(simple_http_cache_singleton), &createCache);
  }

private:
  static std::shared_ptr<Singleton::Instance> createCache() {
    return std::make_shared<SimpleHttpCache>();
  }
};

static Registry::RegisterFactory<SimpleHttpCacheFactory, HttpCacheFactory> register_;

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
