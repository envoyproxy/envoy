#include "source/extensions/http/cache_v2/simple_http_cache/simple_http_cache.h"

#include "envoy/extensions/http/cache_v2/simple_http_cache/v3/config.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/cache_v2/cache_sessions.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace {

constexpr absl::string_view Name = "envoy.extensions.http.cache_v2.simple";

constexpr uint64_t InsertReadChunkSize = 512 * 1024;

class InsertContext {
public:
  static void start(std::shared_ptr<SimpleHttpCache::Entry> entry,
                    std::shared_ptr<CacheProgressReceiver> progress_receiver, HttpSourcePtr source);

private:
  InsertContext(std::shared_ptr<SimpleHttpCache::Entry> entry,
                std::shared_ptr<CacheProgressReceiver> progress_receiver, HttpSourcePtr source);
  void onBody(AdjustedByteRange range, Buffer::InstancePtr buffer, EndStream end_stream);
  void onTrailers(Http::ResponseTrailerMapPtr trailers, EndStream end_stream);
  std::shared_ptr<SimpleHttpCache::Entry> entry_;
  std::shared_ptr<CacheProgressReceiver> progress_receiver_;
  HttpSourcePtr source_;
};

class SimpleHttpCacheReader : public CacheReader {
public:
  SimpleHttpCacheReader(std::shared_ptr<SimpleHttpCache::Entry> entry) : entry_(std::move(entry)) {}
  void getBody(Event::Dispatcher& dispatcher, AdjustedByteRange range,
               GetBodyCallback&& cb) override;

private:
  std::shared_ptr<SimpleHttpCache::Entry> entry_;
};

void SimpleHttpCacheReader::getBody(Event::Dispatcher&, AdjustedByteRange range,
                                    GetBodyCallback&& cb) {
  cb(entry_->body(std::move(range)), EndStream::More);
}

void InsertContext::start(std::shared_ptr<SimpleHttpCache::Entry> entry,
                          std::shared_ptr<CacheProgressReceiver> progress_receiver,
                          HttpSourcePtr source) {
  auto ctx = new InsertContext(std::move(entry), std::move(progress_receiver), std::move(source));
  ctx->source_->getBody(AdjustedByteRange(0, InsertReadChunkSize), [ctx](Buffer::InstancePtr buffer,
                                                                         EndStream end_stream) {
    ctx->onBody(AdjustedByteRange(0, InsertReadChunkSize), std::move(buffer), end_stream);
  });
}

InsertContext::InsertContext(std::shared_ptr<SimpleHttpCache::Entry> entry,
                             std::shared_ptr<CacheProgressReceiver> progress_receiver,
                             HttpSourcePtr source)
    : entry_(std::move(entry)), progress_receiver_(std::move(progress_receiver)),
      source_(std::move(source)) {}

void InsertContext::onBody(AdjustedByteRange range, Buffer::InstancePtr buffer,
                           EndStream end_stream) {
  if (end_stream == EndStream::Reset) {
    progress_receiver_->onInsertFailed(absl::UnavailableError("upstream reset"));
    delete this;
    return;
  }
  if (end_stream == EndStream::End) {
    entry_->setEndStreamAfterBody();
  }
  if (buffer) {
    ASSERT(range.length() >= buffer->length());
    range = AdjustedByteRange(range.begin(), range.begin() + buffer->length());
    entry_->appendBody(std::move(buffer));
  } else if (end_stream == EndStream::More) {
    // Neither buffer nor EndStream::End means we want trailers.
    return source_->getTrailers([this](Http::ResponseTrailerMapPtr trailers, EndStream end_stream) {
      onTrailers(std::move(trailers), end_stream);
    });
  } else {
    range = AdjustedByteRange(0, entry_->bodySize());
  }
  progress_receiver_->onBodyInserted(std::move(range), end_stream == EndStream::End);
  if (end_stream != EndStream::End) {
    AdjustedByteRange next_range(range.end(), range.end() + InsertReadChunkSize);
    return source_->getBody(next_range,
                            [this, next_range](Buffer::InstancePtr buffer, EndStream end_stream) {
                              onBody(next_range, std::move(buffer), end_stream);
                            });
  }
  delete this;
}

void InsertContext::onTrailers(Http::ResponseTrailerMapPtr trailers, EndStream end_stream) {
  if (end_stream == EndStream::Reset) {
    progress_receiver_->onInsertFailed(absl::UnavailableError("upstream reset during trailers"));
  } else {
    entry_->setTrailers(std::move(trailers));
    progress_receiver_->onTrailersInserted(entry_->copyTrailers());
  }
  delete this;
}

} // namespace

Buffer::InstancePtr SimpleHttpCache::Entry::body(AdjustedByteRange range) const {
  absl::ReaderMutexLock lock(mu_);
  return std::make_unique<Buffer::OwnedImpl>(
      absl::string_view{body_}.substr(range.begin(), range.length()));
}

void SimpleHttpCache::Entry::appendBody(Buffer::InstancePtr buf) {
  absl::WriterMutexLock lock(mu_);
  body_ += buf->toString();
}

uint64_t SimpleHttpCache::Entry::bodySize() const {
  absl::ReaderMutexLock lock(mu_);
  return body_.size();
}

Http::ResponseHeaderMapPtr SimpleHttpCache::Entry::copyHeaders() const {
  absl::ReaderMutexLock lock(mu_);
  return Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers_);
}

Http::ResponseTrailerMapPtr SimpleHttpCache::Entry::copyTrailers() const {
  absl::ReaderMutexLock lock(mu_);
  if (!trailers_) {
    return nullptr;
  }
  return Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*trailers_);
}

ResponseMetadata SimpleHttpCache::Entry::metadata() const {
  absl::ReaderMutexLock lock(mu_);
  return metadata_;
}

void SimpleHttpCache::Entry::updateHeadersAndMetadata(Http::ResponseHeaderMapPtr response_headers,
                                                      ResponseMetadata metadata) {
  absl::WriterMutexLock lock(mu_);
  response_headers_ = std::move(response_headers);
  metadata_ = std::move(metadata);
}

void SimpleHttpCache::Entry::setTrailers(Http::ResponseTrailerMapPtr trailers) {
  absl::WriterMutexLock lock(mu_);
  trailers_ = std::move(trailers);
}

void SimpleHttpCache::Entry::setEndStreamAfterBody() {
  absl::WriterMutexLock lock(mu_);
  end_stream_after_body_ = true;
}

CacheInfo SimpleHttpCache::cacheInfo() const {
  CacheInfo cache_info;
  cache_info.name_ = Name;
  return cache_info;
}

void SimpleHttpCache::lookup(LookupRequest&& request, LookupCallback&& callback) {
  LookupResult result;
  {
    absl::ReaderMutexLock lock(mu_);
    auto it = entries_.find(request.key());
    if (it != entries_.end()) {
      result.cache_reader_ = std::make_unique<SimpleHttpCacheReader>(it->second);
      result.response_headers_ = it->second->copyHeaders();
      result.response_metadata_ = it->second->metadata();
      result.response_trailers_ = it->second->copyTrailers();
      result.body_length_ = it->second->bodySize();
    }
  }
  callback(std::move(result));
}

void SimpleHttpCache::evict(Event::Dispatcher&, const Key& key) {
  absl::WriterMutexLock lock(mu_);
  entries_.erase(key);
}

void SimpleHttpCache::updateHeaders(Event::Dispatcher&, const Key& key,
                                    const Http::ResponseHeaderMap& updated_headers,
                                    const ResponseMetadata& updated_metadata) {
  absl::WriterMutexLock lock(mu_);
  auto it = entries_.find(key);
  if (it == entries_.end()) {
    return;
  }
  it->second->updateHeadersAndMetadata(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(updated_headers), updated_metadata);
}

void SimpleHttpCache::insert(Event::Dispatcher&, Key key, Http::ResponseHeaderMapPtr headers,
                             ResponseMetadata metadata, HttpSourcePtr source,
                             std::shared_ptr<CacheProgressReceiver> progress) {
  auto entry = std::make_shared<Entry>(Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*headers),
                                       std::move(metadata));
  {
    absl::WriterMutexLock lock(mu_);
    entries_.emplace(key, entry);
  }
  if (source) {
    progress->onHeadersInserted(std::make_unique<SimpleHttpCacheReader>(entry), std::move(headers),
                                false);
    InsertContext::start(entry, std::move(progress), std::move(source));
  } else {
    progress->onHeadersInserted(nullptr, std::move(headers), true);
  }
}

SINGLETON_MANAGER_REGISTRATION(simple_http_cache_v2_singleton);

class SimpleHttpCacheFactory : public HttpCacheFactory {
public:
  // From UntypedFactory
  std::string name() const override { return std::string(Name); }
  // From TypedFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::http::cache_v2::simple_http_cache::v3::SimpleHttpCacheV2Config>();
  }
  // From HttpCacheFactory
  absl::StatusOr<std::shared_ptr<CacheSessions>>
  getCache(const envoy::extensions::filters::http::cache_v2::v3::CacheV2Config&,
           Server::Configuration::FactoryContext& context) override {
    return context.serverFactoryContext().singletonManager().getTyped<CacheSessions>(
        SINGLETON_MANAGER_REGISTERED_NAME(simple_http_cache_v2_singleton), [&context]() {
          return CacheSessions::create(context, std::make_unique<SimpleHttpCache>());
        });
  }

private:
};

static Registry::RegisterFactory<SimpleHttpCacheFactory, HttpCacheFactory> register_;

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
