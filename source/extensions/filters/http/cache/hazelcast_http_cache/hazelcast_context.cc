#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/http/cache/hazelcast_http_cache/util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

using Envoy::Protobuf::util::MessageDifferencer;

HazelcastLookupContextBase::HazelcastLookupContextBase(HazelcastHttpCache& cache,
                                                       LookupRequest&& request)
    : hz_cache_(cache), lookup_request_(std::move(request)) {
  createVariantKey(lookup_request_.key());
  variant_key_hash_ = stableHashKey(lookup_request_.key());
}

// TODO(enozcan): Support trailers when implemented on the filter side.
void HazelcastLookupContextBase::getTrailers(LookupTrailersCallback&&) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void HazelcastLookupContextBase::handleLookupFailure(absl::string_view message,
                                                     const LookupHeadersCallback& cb,
                                                     bool warn_log) {
  if (warn_log) {
    ENVOY_LOG(warn, "{}", message);
  } else {
    ENVOY_LOG(debug, "{}", message);
  }
  abort_insertion_ = true;
  cb(LookupResult{});
}

void HazelcastLookupContextBase::createVariantKey(Key& raw_key) {
  ASSERT(raw_key.custom_fields_size() == 0);
  ASSERT(raw_key.custom_ints_size() == 0); // Key must be pure.
  if (lookup_request_.varyHeaders().empty()) {
    return;
  }
  std::vector<std::pair<std::string, std::string>> header_strings;
  for (const Http::HeaderEntry& header : lookup_request_.varyHeaders()) {
    header_strings.push_back(std::make_pair(std::string(header.key().getStringView()),
                                            std::string(header.value().getStringView())));
  }
  arrangeVariantHeaders(raw_key, header_strings);
}

// Decoupled from HazelcastLookupContextBase::createVariantKey to be able to test.
void HazelcastLookupContextBase::arrangeVariantHeaders(
    Key& raw_key, std::vector<std::pair<std::string, std::string>>& header_strings) {
  // Different order of headers causes different hash keys even if their both key and value
  // are the same. That is, the following two header lists will cause different hashes for
  // the same response and hence they are sorted before insertion.
  //
  // { {"User-Agent", "desktop"}, {"Accept-Encoding","gzip"} }
  // { {"Accept-Encoding","gzip"}, {"User-Agent", "desktop"} }

  std::sort(header_strings.begin(), header_strings.end(), [](auto& left, auto& right) -> bool {
    // Per https://tools.ietf.org/html/rfc2616#section-4.2 if two different header entries
    // have the same field-name, then their order should not change. For distinct field-named
    // headers the order is not significant but sorted alphabetically here to get the same hash
    // for the same headers.
    return left.first == right.first ? false : left.first < right.first;
  });

  // stableHashKey will create the same variant hashes for the above keys since both
  // have the same custom_fields:
  // [ "Accept-Encoding", "gzip", "User-Agent", "desktop"]
  for (auto& header : header_strings) {
    raw_key.add_custom_fields(std::move(header.first));
    raw_key.add_custom_fields(std::move(header.second));
  }
  // TODO(enozcan): Ensure the generation of the same hash for the same response independent
  //  from the header orders.
  //  Different hashes will be created if the order of values differ for the same
  //  vary header key. The response will not be affected but the same response will
  //  be cached with different keys. i.e. two different hashes exist for the followings
  //  where the only allowed vary header is "accept-language":
  //  - {accept-language: en-US,tr;q=0.8}
  //  - {accept-language: tr;q=0.8,en-US}
}

HazelcastInsertContextBase::HazelcastInsertContextBase(LookupContext& lookup_context,
                                                       HazelcastHttpCache& cache)
    : hz_cache_(cache), max_body_size_(cache.maxBodyBytes()),
      variant_key_hash_(static_cast<HazelcastLookupContextBase&>(lookup_context).variantKeyHash()),
      variant_key_(static_cast<HazelcastLookupContextBase&>(lookup_context).variantKey()),
      abort_insertion_(static_cast<HazelcastLookupContextBase&>(lookup_context).isAborted()) {}

// TODO(enozcan): Support trailers when implemented on the filter side.
void HazelcastInsertContextBase::insertTrailers(const Http::ResponseTrailerMap&) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

UnifiedLookupContext::UnifiedLookupContext(HazelcastHttpCache& cache, LookupRequest&& request)
    : HazelcastLookupContextBase(cache, std::move(request)) {}

void UnifiedLookupContext::getHeaders(LookupHeadersCallback&& cb) {
  ENVOY_LOG(debug, "Looking up unified response with key hash: {}u", variant_key_hash_);
  try {
    response_ = hz_cache_.getResponse(variant_key_hash_);
  } catch (HazelcastClientOfflineException& e) {
    handleLookupFailure("Hazelcast cluster connection is lost! Aborting all lookups and "
                        "insertions until the connection is restored...",
                        cb);
    return;
  } catch (OperationTimeoutException& e) {
    handleLookupFailure("Operation timed out during cache lookup.", cb);
    return;
  } catch (std::exception& e) {
    handleLookupFailure(fmt::format("Lookup to cache has failed: {}", e.what()), cb);
    return;
  }
  if (response_) {
    ENVOY_LOG(debug, "Found unified response: [key hash: {}u, body size: {}]", variant_key_hash_,
              response_->body().length());
    if (!MessageDifferencer::Equals(response_->header().variantKey(), variantKey())) {
      // As cache filter denotes, a secondary check other than the hash key
      // is performed here. If a different response is found with the same
      // hash (probably on hash collisions), the new response is denied to
      // be cached and the old one remains.
      handleLookupFailure(
          "Mismatched keys found for key hash: " + std::to_string(variant_key_hash_), cb, false);
      return;
    }
    cb(lookup_request_.makeLookupResult(std::move(response_->header().headerMap()),
                                        response_->body().length()));
  } else {
    ENVOY_LOG(debug, "Missed unified response lookup for key hash: {}u", variant_key_hash_);
    cb(LookupResult{});
  }
}

void UnifiedLookupContext::getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) {
  ENVOY_LOG(debug, "Getting unified body (total length = {}) with range: [{}, {}]",
            response_->body().length(), range.begin(), range.end());
  ASSERT(response_ && !abort_insertion_);
  ASSERT(range.end() <= response_->body().length());
  hazelcast::byte* data = response_->body().begin() + range.begin();
  cb(std::make_unique<Buffer::OwnedImpl>(data, range.length()));
}

UnifiedInsertContext::UnifiedInsertContext(LookupContext& lookup_context, HazelcastHttpCache& cache)
    : HazelcastInsertContextBase(lookup_context, cache) {
  // Unlike DIVIDED mode, lock is not tried to be acquired before insertion here.
  // The reason behind tryLock before insertion in DIVIDED context is to prevent
  // multiple body entry insertions by different contexts simultaneously. Since
  // there is only one entry is to be inserted in UNIFIED mode, a locking mechanism
  // is not used here. Multiple contexts can attempt to insert a response for the
  // same hash key at the same time and can override an existing one.
}

void UnifiedInsertContext::insertHeaders(const Http::ResponseHeaderMap& response_headers,
                                         bool end_stream) {
  if (abort_insertion_) {
    return;
  }
  ASSERT(!committed_end_stream_);
  header_map_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers);
  if (end_stream) {
    insertResponse();
  }
}

void UnifiedInsertContext::insertBody(const Buffer::Instance& chunk,
                                      InsertCallback ready_for_next_chunk, bool end_stream) {
  if (abort_insertion_) {
    if (ready_for_next_chunk) {
      ready_for_next_chunk(false);
    }
    return;
  }
  ASSERT(!committed_end_stream_);
  size_t buffer_length = buffer_vector_.size();
  size_t allowed_size = max_body_size_ - buffer_length;
  if (allowed_size > chunk.length()) {
    buffer_vector_.resize(buffer_length + chunk.length());
    chunk.copyOut(0, chunk.length(), buffer_vector_.data() + buffer_length);
  } else {
    // Store the body copied until now and abort the further attempts.
    buffer_vector_.resize(max_body_size_);
    chunk.copyOut(0, allowed_size, buffer_vector_.data() + buffer_length);
    insertResponse();
    ready_for_next_chunk(false);
    return;
  }

  if (end_stream) {
    insertResponse();
  } else if (ready_for_next_chunk) {
    ready_for_next_chunk(true);
  }
}

void UnifiedInsertContext::insertResponse() {
  ASSERT(!abort_insertion_);
  ASSERT(!committed_end_stream_);
  ENVOY_LOG(debug, "Inserting unified entry with key hash {}u if absent", variant_key_hash_);
  committed_end_stream_ = true;

  // Versions are not necessary for unified entries. Hence passing arbitrary 0 here.
  HazelcastHeaderEntry header(std::move(header_map_), std::move(variant_key_),
                              buffer_vector_.size(), 0);
  HazelcastBodyEntry body(std::move(buffer_vector_), 0);

  HazelcastResponseEntry entry(std::move(header), std::move(body));
  try {
    hz_cache_.putResponse(variant_key_hash_, entry);
  } catch (HazelcastClientOfflineException& e) {
    ENVOY_LOG(warn, "Hazelcast cluster connection is lost! Failed to insert response.");
  } catch (OperationTimeoutException& e) {
    ENVOY_LOG(warn, "Operation timed out during cache insertion.");
  } catch (std::exception& e) {
    ENVOY_LOG(warn, "Response insertion to cache has failed: {}", e.what());
  }
}

DividedLookupContext::DividedLookupContext(HazelcastHttpCache& cache, LookupRequest&& request)
    : HazelcastLookupContextBase(cache, std::move(request)),
      body_partition_size_(cache.bodySizePerEntry()){};

void DividedLookupContext::getHeaders(LookupHeadersCallback&& cb) {
  ENVOY_LOG(debug, "Looking up divided header with key hash: {}u", variant_key_hash_);
  HazelcastHeaderPtr header_entry;
  try {
    header_entry = hz_cache_.getHeader(variant_key_hash_);
  } catch (HazelcastClientOfflineException& e) {
    handleLookupFailure("Hazelcast cluster connection is lost! Aborting lookups and "
                        "insertions until the connection is restored.",
                        cb);
    return;
  } catch (OperationTimeoutException& e) {
    handleLookupFailure("Operation timed out during cache lookup.", cb);
    return;
  } catch (std::exception& e) {
    handleLookupFailure(fmt::format("Lookup to cache has failed: {}", e.what()), cb);
    return;
  }
  if (header_entry) {
    ENVOY_LOG(debug, "Found divided response: [key: {}u, version: {}, body size: {}]",
              variant_key_hash_, header_entry->version(), header_entry->bodySize());
    if (!MessageDifferencer::Equals(header_entry->variantKey(), variantKey())) {
      handleLookupFailure(
          "Mismatched keys found for key hash: " + std::to_string(variant_key_hash_), cb, false);
      return;
    }
    this->total_body_size_ = header_entry->bodySize();
    this->version_ = header_entry->version();
    this->found_header_ = true;
    cb(lookup_request_.makeLookupResult(std::move(header_entry->headerMap()), total_body_size_));
  } else {
    ENVOY_LOG(debug, "Missed divided response lookup for key hash: {}u", variant_key_hash_);
    cb(LookupResult{});
  }
}

// Hence bodies are stored partially on the cache (see hazelcast_cache_entry.h for details),
// the returning buffer from this function can have a size of at most body_partition_size_.
// The caller (filter) has to check range and make another getBody request if needed.
//
// For instance, for a response of which body is 5 KB length, the cached entries will look
// like the following with 2 KB of body_partition_size_ configured:
//
// <variant_key_hash(long)> --> HazelcastHeaderEntry(response headers)
//
// <variant_key_hash(string) + "#0"> --> HazelcastBodyEntry(0-2 KB)
// <variant_key_hash(string) + "#1"> --> HazelcastBodyEntry(2-4 KB)
// <variant_key_hash(string) + "#2"> --> HazelcastBodyEntry(4-5 KB)
//
void DividedLookupContext::getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) {
  ASSERT(range.end() <= total_body_size_);
  ASSERT(found_header_, "Header lookup is missed.");

  // Lookup for only one body partition which includes the range.begin().
  uint64_t body_index = range.begin() / body_partition_size_;
  HazelcastBodyPtr body;
  ENVOY_LOG(debug, "Looking up divided body with key hash: {}u, order: {}", variant_key_hash_,
            body_index);
  try {
    body = hz_cache_.getBody(variant_key_hash_, body_index);
  } catch (HazelcastClientOfflineException& e) {
    handleBodyLookupFailure("Hazelcast cluster connection is lost! Aborting lookups and "
                            "insertions until the connection is restored...",
                            cb);
    return;
  } catch (OperationTimeoutException& e) {
    handleBodyLookupFailure("Operation timed out during cache lookup.", cb);
    return;
  } catch (std::exception& e) {
    handleBodyLookupFailure(fmt::format("Lookup to cache for body entry has failed: {}", e.what()),
                            cb);
    return;
  }

  if (body) {
    ENVOY_LOG(debug, "Found divided body: [key: {}u + \"#{}\", version: {}, size: {}]",
              variant_key_hash_, body_index, body->version(), body->length());
    if (body->version() != version_) {
      hz_cache_.onVersionMismatch(variant_key_hash_, version_, total_body_size_);
      handleBodyLookupFailure(
          fmt::format("Body version mismatched with header for "
                      "key {}u at body: {}. Aborting lookup and performing cleanup.",
                      variant_key_hash_, body_index),
          cb, false);
      return;
    }
    uint64_t offset = (range.begin() % body_partition_size_);
    hazelcast::byte* data = body->begin() + offset;
    if (range.end() < (body_index + 1) * body_partition_size_) {
      // No other body partition is needed since this one satisfies the
      // range. Callback with the appropriate body bytes.
      cb(std::make_unique<Buffer::OwnedImpl>(data, range.length()));
    } else {
      // The range requests bytes from the next body partition as well.
      // Callback with the bytes until the end of the current partition.
      cb(std::make_unique<Buffer::OwnedImpl>(data, body->length() - offset));
    }
  } else {
    // Body partition is expected to reside in the cache but lookup is failed.
    hz_cache_.onMissingBody(variant_key_hash_, version_, total_body_size_);
    handleBodyLookupFailure(fmt::format("Found missing body for key {}u at index: {}. Response "
                                        "with body size {} has been cleaned up from the cache.",
                                        variant_key_hash_, body_index, total_body_size_),
                            cb, false);
  }
};

void DividedLookupContext::handleBodyLookupFailure(absl::string_view message,
                                                   const LookupBodyCallback& cb, bool warn_log) {
  if (warn_log) {
    ENVOY_LOG(warn, "{}", message);
  } else {
    ENVOY_LOG(debug, "{}", message);
  }
  cb(nullptr);
}

DividedInsertContext::DividedInsertContext(LookupContext& lookup_context, HazelcastHttpCache& cache)
    : HazelcastInsertContextBase(lookup_context, cache),
      body_partition_size_(cache.bodySizePerEntry()), version_(createVersion()) {
  try {
    // To prevent multiple insertion contexts to create the same response in the cache,
    // mark only one of them responsible for the insertion using Hazelcast map key locks.
    // If key is not locked, it will be acquired here and only one insertion context
    // will be responsible for the insertion. This is also valid when multiple cache
    // filters from different proxies are connected to the same Hazelcast cluster.
    // There is no such a mechanism for UNIFIED mode.
    insertion_allowed_ = hz_cache_.tryLock(variant_key_hash_);
    return;
  } catch (HazelcastClientOfflineException& e) {
    ENVOY_LOG(warn, "Hazelcast cluster connection is lost! Aborting lookups and insertions until "
                    "the connection is restored...");
  } catch (OperationTimeoutException& e) {
    ENVOY_LOG(warn, "Operation timed out during tryLock!");
  } catch (std::exception& e) {
    ENVOY_LOG(warn, "Lock trial has failed: {}", e.what());
  }
  insertion_allowed_ = false;
}

void DividedInsertContext::insertHeaders(const Http::ResponseHeaderMap& response_headers,
                                         bool end_stream) {
  if (abort_insertion_ || !insertion_allowed_) {
    return;
  }
  ASSERT(!committed_end_stream_);
  header_map_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers);
  if (end_stream) {
    insertHeader();
  }
}

// Body insertions in DIVIDED cache mode must be performed over a fixed sized buffer
// hence continuity of the body partitions are ensured. To do this, insertion chunk's
// content is copied into a local buffer every time insertBody is called. And it is
// flushed when it reaches the maximum capacity (body_partition_size_).
void DividedInsertContext::insertBody(const Buffer::Instance& chunk,
                                      InsertCallback ready_for_next_chunk, bool end_stream) {
  if (abort_insertion_ || !insertion_allowed_) {
    ENVOY_LOG(debug, "Skipping insertion for the hash key: {}u", variant_key_hash_);
    if (ready_for_next_chunk) {
      ready_for_next_chunk(false);
    }
    return;
  }
  ASSERT(!committed_end_stream_);
  uint64_t copied_bytes = 0;
  uint64_t allowed_bytes =
      max_body_size_ - (body_order_ * body_partition_size_ + buffer_vector_.size());
  uint64_t remaining_bytes = allowed_bytes < chunk.length() ? allowed_bytes : chunk.length();
  bool trimmed = remaining_bytes == allowed_bytes;
  while (remaining_bytes) {
    uint64_t available_bytes = body_partition_size_ - buffer_vector_.size();
    if (available_bytes < remaining_bytes) {
      // This chunk is going to fill the buffer. Copy as much bytes as possible
      // into the buffer, flush the buffer and continue with the remaining bytes.
      copyIntoLocalBuffer(copied_bytes, available_bytes, chunk);
      ASSERT(buffer_vector_.size() == body_partition_size_);
      remaining_bytes -= available_bytes;
      if (!flushBuffer()) {
        // Abort insertion if one of the body insertions fails.
        if (ready_for_next_chunk) {
          ready_for_next_chunk(false);
        }
        return;
      }
    } else {
      // Copy all the bytes starting from chunk[copied_bytes] into buffer. Current
      // buffer can hold the remaining data.
      copyIntoLocalBuffer(copied_bytes, remaining_bytes, chunk);
      break;
    }
  }

  if (end_stream || trimmed) {
    // Header shouldn't be inserted before body insertions are completed.
    // Total body size in the header entry is computed via inserted body partitions.
    if (flushBuffer()) {
      // Header insertion is performed only when all bodies are stored.
      // Otherwise, insertion will be aborted and another insert context
      // will store the response by overriding body entries flushed so far.
      insertHeader();
    }
  }
  if (ready_for_next_chunk) {
    ready_for_next_chunk(!trimmed);
  }
}

void DividedInsertContext::copyIntoLocalBuffer(uint64_t& offset, uint64_t size,
                                               const Buffer::Instance& source) {
  uint64_t current_size = buffer_vector_.size();
  buffer_vector_.resize(current_size + size);
  source.copyOut(offset, size, buffer_vector_.data() + current_size);
  offset += size;
};

bool DividedInsertContext::flushBuffer() {
  ASSERT(!abort_insertion_);
  ASSERT(insertion_allowed_);
  if (buffer_vector_.empty()) {
    return true;
  }
  total_body_size_ += buffer_vector_.size();
  HazelcastBodyEntry bodyEntry(std::move(buffer_vector_), version_);
  buffer_vector_.clear();
  try {
    hz_cache_.putBody(variant_key_hash_, body_order_++, bodyEntry);
  } catch (HazelcastClientOfflineException& e) {
    ENVOY_LOG(warn, "Hazelcast cluster connection is lost!");
    return false;
  } catch (OperationTimeoutException& e) {
    ENVOY_LOG(warn, "Operation timed out during body insertion.");
    return false;
  } catch (std::exception& e) {
    ENVOY_LOG(warn, "Body insertion to cache has failed: {}", e.what());
    return false;
  }
  if (body_order_ == ConfigUtil::partitionWarnLimit()) {
    ENVOY_LOG(warn, "Number of body partitions for a response has been reached {} (or more).",
              ConfigUtil::partitionWarnLimit());
    ENVOY_LOG(info, "Having so many partitions might cause performance drop "
                    "as well as extra memory usage. Consider increasing body "
                    "partition size.");
  }
  return true;
}

void DividedInsertContext::insertHeader() {
  ASSERT(!abort_insertion_);
  ASSERT(insertion_allowed_);
  ASSERT(!committed_end_stream_);
  committed_end_stream_ = true;
  HazelcastHeaderEntry header(std::move(header_map_), std::move(variant_key_), total_body_size_,
                              version_);
  try {
    hz_cache_.putHeader(variant_key_hash_, header);
    hz_cache_.unlock(variant_key_hash_);
    ENVOY_LOG(debug, "Inserted header entry with key {}u", variant_key_hash_);
    // To handle leftover locks in a failure, hazelcast.lock.max.lease.time.seconds property
    // must be set to a reasonable value on the server side. It is Long.MAX by default.
    // To make this independent from the server configuration, tryLock with leaseTime
    // option can be used when available in a future release of cpp client as it's implemented
    // at: https://github.com/hazelcast/hazelcast-cpp-client/issues/579
    // TODO(enozcan): Use tryLock with leaseTime when released for Hazelcast cpp client.
  } catch (HazelcastClientOfflineException& e) {
    ENVOY_LOG(warn, "Hazelcast Connection is offline!");
  } catch (std::exception& e) {
    ENVOY_LOG(warn, "Failed to complete response insertion: {}", e.what());
  }
}

int32_t DividedInsertContext::createVersion() {
  // We do not need a strong uniformity or randomness here. Even
  // the versions of two different header entries with distinct
  // hash keys are the same, this will not cause a problem at all.
  // We only need a stamp for bodies which inserted for this context.
  // Since this version is stored in cache entries, a 32-bit random
  // derived from the 64-bit one is preferred here.
  // Range: from (int32.MIN + 1) to (int32.MAX - 1), inclusive.
  uint64_t rand64 = hz_cache_.random();
  uint64_t max = std::numeric_limits<int32_t>::max();
  return (rand64 % (max * 2)) - max;
}

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
