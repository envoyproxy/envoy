#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "envoy/common/pure.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/macros.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

// Used by ASSERTs to validate internal consistency. E.g. valid HTTP header keys/values should
// never contain embedded NULLs.
static inline bool validHeaderString(absl::string_view s) {
  // If you modify this list of illegal embedded characters you will probably
  // want to change header_map_fuzz_impl_test at the same time.
  for (const char c : s) {
    switch (c) {
    case '\0':
      FALLTHRU;
    case '\r':
      FALLTHRU;
    case '\n':
      return false;
    default:
      continue;
    }
  }
  return true;
}

/**
 * Wrapper for a lower case string used in header operations to generally avoid needless case
 * insensitive compares.
 */
class LowerCaseString {
public:
  LowerCaseString(LowerCaseString&& rhs) noexcept : string_(std::move(rhs.string_)) {
    ASSERT(valid());
  }
  LowerCaseString(const LowerCaseString& rhs) : string_(rhs.string_) { ASSERT(valid()); }
  explicit LowerCaseString(const std::string& new_string) : string_(new_string) {
    ASSERT(valid());
    lower();
  }

  const std::string& get() const { return string_; }
  bool operator==(const LowerCaseString& rhs) const { return string_ == rhs.string_; }
  bool operator!=(const LowerCaseString& rhs) const { return string_ != rhs.string_; }
  bool operator<(const LowerCaseString& rhs) const { return string_.compare(rhs.string_) < 0; }

private:
  void lower() { std::transform(string_.begin(), string_.end(), string_.begin(), tolower); }
  bool valid() const { return validHeaderString(string_); }

  std::string string_;
};

/**
 * Lower case string hasher.
 */
struct LowerCaseStringHash {
  size_t operator()(const LowerCaseString& value) const { return HashUtil::xxHash64(value.get()); }
};

/**
 * Convenient type for unordered set of lower case string.
 */
using LowerCaseStrUnorderedSet = std::unordered_set<LowerCaseString, LowerCaseStringHash>;

/**
 * Convenient type for a vector of lower case string and string pair.
 */
using LowerCaseStrPairVector =
    std::vector<std::pair<const Http::LowerCaseString, const std::string>>;

/**
 * This is a string implementation for use in header processing. It is heavily optimized for
 * performance. It supports 3 different types of storage and can switch between them:
 * 1) A reference.
 * 2) Interned string.
 * 3) Heap allocated storage.
 */
class HeaderString {
public:
  enum class Type { Inline, Reference, Dynamic };

  /**
   * Default constructor. Sets up for inline storage.
   */
  HeaderString();

  /**
   * Constructor for a string reference.
   * @param ref_value MUST point to data that will live beyond the lifetime of any request/response
   *        using the string (since a codec may optimize for zero copy).
   */
  explicit HeaderString(const LowerCaseString& ref_value);

  /**
   * Constructor for a string reference.
   * @param ref_value MUST point to data that will live beyond the lifetime of any request/response
   *        using the string (since a codec may optimize for zero copy).
   */
  explicit HeaderString(const std::string& ref_value);

  HeaderString(HeaderString&& move_value) noexcept;
  ~HeaderString();

  /**
   * Append data to an existing string. If the string is a reference string the reference data is
   * not copied.
   */
  void append(const char* data, uint32_t size);

  /**
   * @return the modifiable backing buffer (either inline or heap allocated).
   */
  char* buffer() { return buffer_.dynamic_; }

  /**
   * Trim trailing whitespaces from the HeaderString.
   * v1.12 supports both Inline and Dynamic, but not Reference type.
   */
  void rtrim();

  /**
   * Get an absl::string_view. It will NOT be NUL terminated!
   *
   * @return an absl::string_view.
   */
  absl::string_view getStringView() const { return {buffer_.ref_, string_length_}; }

  /**
   * Return the string to a default state. Reference strings are not touched. Both inline/dynamic
   * strings are reset to zero size.
   */
  void clear();

  /**
   * @return whether the string is empty or not.
   */
  bool empty() const { return string_length_ == 0; }

  // Looking for find? Use getStringView().find()

  /**
   * Set the value of the string by copying data into it. This overwrites any existing string.
   */
  void setCopy(const char* data, uint32_t size);

  /**
   * Set the value of the string by copying data into it. This overwrites any existing string.
   */
  void setCopy(absl::string_view view);

  /**
   * Set the value of the string to an integer. This overwrites any existing string.
   */
  void setInteger(uint64_t value);

  /**
   * Set the value of the string to a string reference.
   * @param ref_value MUST point to data that will live beyond the lifetime of any request/response
   *        using the string (since a codec may optimize for zero copy).
   */
  void setReference(const std::string& ref_value);

  /**
   * @return the size of the string, not including the null terminator.
   */
  uint32_t size() const { return string_length_; }

  /**
   * @return the type of backing storage for the string.
   */
  Type type() const { return type_; }

  bool operator==(const char* rhs) const { return getStringView() == absl::string_view(rhs); }
  bool operator==(absl::string_view rhs) const { return getStringView() == rhs; }
  bool operator!=(const char* rhs) const { return getStringView() != absl::string_view(rhs); }
  bool operator!=(absl::string_view rhs) const { return getStringView() != rhs; }

private:
  union Buffer {
    // This should reference inline_buffer_ for Type::Inline.
    char* dynamic_;
    const char* ref_;
  } buffer_;

  // Capacity in both Type::Inline and Type::Dynamic cases must be at least MinDynamicCapacity in
  // header_map_impl.cc.
  union {
    char inline_buffer_[128];
    // Since this is a union, this is only valid for type_ == Type::Dynamic.
    uint32_t dynamic_capacity_;
  };

  void freeDynamic();
  bool valid() const;

  uint32_t string_length_;
  Type type_;
};

/**
 * Encapsulates an individual header entry (including both key and value).
 */
class HeaderEntry {
public:
  virtual ~HeaderEntry() = default;

  /**
   * @return the header key.
   */
  virtual const HeaderString& key() const PURE;

  /**
   * Set the header value by copying data into it (deprecated, use absl::string_view variant
   * instead).
   * TODO(htuch): Cleanup deprecated call sites.
   */
  virtual void value(const char* value, uint32_t size) PURE;

  /**
   * Set the header value by copying data into it.
   */
  virtual void value(absl::string_view value) PURE;

  /**
   * Set the header value by copying an integer into it.
   */
  virtual void value(uint64_t value) PURE;

  /**
   * Set the header value by copying the value in another header entry.
   */
  virtual void value(const HeaderEntry& header) PURE;

  /**
   * @return the header value.
   */
  virtual const HeaderString& value() const PURE;

  /**
   * @return the header value.
   */
  virtual HeaderString& value() PURE;

private:
  void value(const char*); // Do not allow auto conversion to std::string
};

/**
 * The following defines all headers that Envoy allows direct access to inside of the header map.
 * In practice, these are all headers used during normal Envoy request flow processing. This allows
 * O(1) access to these headers without even a hash lookup.
 */
#define ALL_INLINE_HEADERS(HEADER_FUNC)                                                            \
  HEADER_FUNC(Accept)                                                                              \
  HEADER_FUNC(AcceptEncoding)                                                                      \
  HEADER_FUNC(AccessControlRequestHeaders)                                                         \
  HEADER_FUNC(AccessControlRequestMethod)                                                          \
  HEADER_FUNC(AccessControlAllowOrigin)                                                            \
  HEADER_FUNC(AccessControlAllowHeaders)                                                           \
  HEADER_FUNC(AccessControlAllowMethods)                                                           \
  HEADER_FUNC(AccessControlAllowCredentials)                                                       \
  HEADER_FUNC(AccessControlExposeHeaders)                                                          \
  HEADER_FUNC(AccessControlMaxAge)                                                                 \
  HEADER_FUNC(Authorization)                                                                       \
  HEADER_FUNC(CacheControl)                                                                        \
  HEADER_FUNC(ClientTraceId)                                                                       \
  HEADER_FUNC(Connection)                                                                          \
  HEADER_FUNC(ContentEncoding)                                                                     \
  HEADER_FUNC(ContentLength)                                                                       \
  HEADER_FUNC(ContentType)                                                                         \
  HEADER_FUNC(Date)                                                                                \
  HEADER_FUNC(EnvoyAttemptCount)                                                                   \
  HEADER_FUNC(EnvoyDegraded)                                                                       \
  HEADER_FUNC(EnvoyDecoratorOperation)                                                             \
  HEADER_FUNC(EnvoyDownstreamServiceCluster)                                                       \
  HEADER_FUNC(EnvoyDownstreamServiceNode)                                                          \
  HEADER_FUNC(EnvoyExpectedRequestTimeoutMs)                                                       \
  HEADER_FUNC(EnvoyExternalAddress)                                                                \
  HEADER_FUNC(EnvoyForceTrace)                                                                     \
  HEADER_FUNC(EnvoyHedgeOnPerTryTimeout)                                                           \
  HEADER_FUNC(EnvoyImmediateHealthCheckFail)                                                       \
  HEADER_FUNC(EnvoyInternalRequest)                                                                \
  HEADER_FUNC(EnvoyIpTags)                                                                         \
  HEADER_FUNC(EnvoyMaxRetries)                                                                     \
  HEADER_FUNC(EnvoyOriginalPath)                                                                   \
  HEADER_FUNC(EnvoyOriginalUrl)                                                                    \
  HEADER_FUNC(EnvoyOverloaded)                                                                     \
  HEADER_FUNC(EnvoyRateLimited)                                                                    \
  HEADER_FUNC(EnvoyRetryOn)                                                                        \
  HEADER_FUNC(EnvoyRetryGrpcOn)                                                                    \
  HEADER_FUNC(EnvoyRetriableStatusCodes)                                                           \
  HEADER_FUNC(EnvoyRetriableHeaderNames)                                                           \
  HEADER_FUNC(EnvoyUpstreamAltStatName)                                                            \
  HEADER_FUNC(EnvoyUpstreamCanary)                                                                 \
  HEADER_FUNC(EnvoyUpstreamHealthCheckedCluster)                                                   \
  HEADER_FUNC(EnvoyUpstreamRequestPerTryTimeoutMs)                                                 \
  HEADER_FUNC(EnvoyUpstreamRequestTimeoutAltResponse)                                              \
  HEADER_FUNC(EnvoyUpstreamRequestTimeoutMs)                                                       \
  HEADER_FUNC(EnvoyUpstreamServiceTime)                                                            \
  HEADER_FUNC(Etag)                                                                                \
  HEADER_FUNC(Expect)                                                                              \
  HEADER_FUNC(ForwardedClientCert)                                                                 \
  HEADER_FUNC(ForwardedFor)                                                                        \
  HEADER_FUNC(ForwardedProto)                                                                      \
  HEADER_FUNC(GrpcAcceptEncoding)                                                                  \
  HEADER_FUNC(GrpcMessage)                                                                         \
  HEADER_FUNC(GrpcStatus)                                                                          \
  HEADER_FUNC(GrpcTimeout)                                                                         \
  HEADER_FUNC(Host)                                                                                \
  HEADER_FUNC(KeepAlive)                                                                           \
  HEADER_FUNC(LastModified)                                                                        \
  HEADER_FUNC(Location)                                                                            \
  HEADER_FUNC(Method)                                                                              \
  HEADER_FUNC(NoChunks)                                                                            \
  HEADER_FUNC(Origin)                                                                              \
  HEADER_FUNC(OtSpanContext)                                                                       \
  HEADER_FUNC(Path)                                                                                \
  HEADER_FUNC(Protocol)                                                                            \
  HEADER_FUNC(ProxyConnection)                                                                     \
  HEADER_FUNC(Referer)                                                                             \
  HEADER_FUNC(RequestId)                                                                           \
  HEADER_FUNC(Scheme)                                                                              \
  HEADER_FUNC(Server)                                                                              \
  HEADER_FUNC(Status)                                                                              \
  HEADER_FUNC(TE)                                                                                  \
  HEADER_FUNC(TransferEncoding)                                                                    \
  HEADER_FUNC(Upgrade)                                                                             \
  HEADER_FUNC(UserAgent)                                                                           \
  HEADER_FUNC(Vary)                                                                                \
  HEADER_FUNC(Via)

/**
 * The following functions are defined for each inline header above. E.g., for ContentLength we
 * have:
 *
 * ContentLength() -> returns the header entry if it exists or nullptr.
 * insertContentLength() -> inserts the header if it does not exist, and returns a reference to it.
 * removeContentLength() -> removes the header if it exists.
 */
#define DEFINE_INLINE_HEADER(name)                                                                 \
  virtual const HeaderEntry* name() const PURE;                                                    \
  virtual HeaderEntry* name() PURE;                                                                \
  virtual HeaderEntry& insert##name() PURE;                                                        \
  virtual void remove##name() PURE;

/**
 * Wraps a set of HTTP headers.
 */
class HeaderMap {
public:
  virtual ~HeaderMap() = default;

  ALL_INLINE_HEADERS(DEFINE_INLINE_HEADER)

  /**
   * Add a reference header to the map. Both key and value MUST point to data that will live beyond
   * the lifetime of any request/response using the string (since a codec may optimize for zero
   * copy). The key will not be copied and a best effort will be made not to
   * copy the value (but this may happen when comma concatenating, see below).
   *
   * Calling addReference multiple times for the same header will result in:
   * - Comma concatenation for predefined inline headers.
   * - Multiple headers being present in the HeaderMap for other headers.
   *
   * @param key specifies the name of the header to add; it WILL NOT be copied.
   * @param value specifies the value of the header to add; it WILL NOT be copied.
   */
  virtual void addReference(const LowerCaseString& key, const std::string& value) PURE;

  /**
   * Add a header with a reference key to the map. The key MUST point to data that will live beyond
   * the lifetime of any request/response using the string (since a codec may optimize for zero
   * copy). The value will be copied.
   *
   * Calling addReference multiple times for the same header will result in:
   * - Comma concatenation for predefined inline headers.
   * - Multiple headers being present in the HeaderMap for other headers.
   *
   * @param key specifies the name of the header to add; it WILL NOT be copied.
   * @param value specifies the value of the header to add; it WILL be copied.
   */
  virtual void addReferenceKey(const LowerCaseString& key, uint64_t value) PURE;

  /**
   * Add a header with a reference key to the map. The key MUST point to point to data that will
   * live beyond the lifetime of any request/response using the string (since a codec may optimize
   * for zero copy). The value will be copied.
   *
   * Calling addReference multiple times for the same header will result in:
   * - Comma concatenation for predefined inline headers.
   * - Multiple headers being present in the HeaderMap for other headers.
   *
   * @param key specifies the name of the header to add; it WILL NOT be copied.
   * @param value specifies the value of the header to add; it WILL be copied.
   */
  virtual void addReferenceKey(const LowerCaseString& key, const std::string& value) PURE;

  /**
   * Add a header by copying both the header key and the value.
   *
   * Calling addCopy multiple times for the same header will result in:
   * - Comma concatenation for predefined inline headers.
   * - Multiple headers being present in the HeaderMap for other headers.
   *
   * @param key specifies the name of the header to add; it WILL be copied.
   * @param value specifies the value of the header to add; it WILL be copied.
   */
  virtual void addCopy(const LowerCaseString& key, uint64_t value) PURE;

  /**
   * Add a header by copying both the header key and the value.
   *
   * Calling addCopy multiple times for the same header will result in:
   * - Comma concatenation for predefined inline headers.
   * - Multiple headers being present in the HeaderMap for other headers.
   *
   * @param key specifies the name of the header to add; it WILL be copied.
   * @param value specifies the value of the header to add; it WILL be copied.
   */
  virtual void addCopy(const LowerCaseString& key, const std::string& value) PURE;

  /**
   * Set a reference header in the map. Both key and value MUST point to data that will live beyond
   * the lifetime of any request/response using the string (since a codec may optimize for zero
   * copy). Nothing will be copied.
   *
   * Calling setReference multiple times for the same header will result in only the last header
   * being present in the HeaderMap.
   *
   * @param key specifies the name of the header to set; it WILL NOT be copied.
   * @param value specifies the value of the header to set; it WILL NOT be copied.
   */
  virtual void setReference(const LowerCaseString& key, const std::string& value) PURE;

  /**
   * Set a header with a reference key in the map. The key MUST point to point to data that will
   * live beyond the lifetime of any request/response using the string (since a codec may optimize
   * for zero copy). The value will be copied.
   *
   * Calling setReferenceKey multiple times for the same header will result in only the last header
   * being present in the HeaderMap.
   *
   * @param key specifies the name of the header to set; it WILL NOT be copied.
   * @param value specifies the value of the header to set; it WILL be copied.
   */
  virtual void setReferenceKey(const LowerCaseString& key, const std::string& value) PURE;

  /**
   * HeaderMap contains an internal byte size count, updated as entries are added, removed, or
   * modified through the HeaderMap interface. However, HeaderEntries can be accessed and modified
   * by reference so that the HeaderMap can no longer accurately update the internal byte size
   * count.
   *
   * Calling byteSize before a HeaderEntry is accessed will return the internal byte size count. The
   * value is cleared when a HeaderEntry is accessed, and the value is updated and set again when
   * refreshByteSize is called.
   *
   * To guarantee an accurate byte size count, call refreshByteSize.
   *
   * @return uint64_t the approximate size of the header map in bytes if valid.
   */
  virtual absl::optional<uint64_t> byteSize() const PURE;

  /**
   * This returns the sum of the byte sizes of the keys and values in the HeaderMap. This also
   * updates and sets the byte size count.
   *
   * To guarantee an accurate byte size count, use this. If it is known HeaderEntries have not been
   * manipulated since a call to refreshByteSize, it is safe to use byteSize.
   *
   * @return uint64_t the approximate size of the header map in bytes.
   */
  virtual uint64_t refreshByteSize() PURE;

  /**
   * This returns the sum of the byte sizes of the keys and values in the HeaderMap.
   *
   * This iterates over the HeaderMap to calculate size and should only be called directly when the
   * user wants an explicit recalculation of the byte size.
   *
   * @return uint64_t the approximate size of the header map in bytes.
   */
  virtual uint64_t byteSizeInternal() const PURE;

  /**
   * Get a header by key.
   * @param key supplies the header key.
   * @return the header entry if it exists otherwise nullptr.
   */
  virtual const HeaderEntry* get(const LowerCaseString& key) const PURE;
  virtual HeaderEntry* get(const LowerCaseString& key) PURE;

  /**
   * This is a wrapper for the return result from getAll(). It avoids a copy when translating from
   * non-const HeaderEntry to const HeaderEntry and only provides const access to the result.
   */
  using NonConstGetResult = absl::InlinedVector<HeaderEntry*, 1>;
  class GetResult {
  public:
    GetResult() = default;
    explicit GetResult(NonConstGetResult&& result) : result_(std::move(result)) {}

    bool empty() const { return result_.empty(); }
    size_t size() const { return result_.size(); }
    const HeaderEntry* operator[](size_t i) const { return result_[i]; }

  private:
    NonConstGetResult result_;
  };

  /**
   * Get a header by key.
   * @param key supplies the header key.
   * @return all header entries matching the key.
   */
  virtual GetResult getAll(const LowerCaseString& key) const PURE;

  // aliases to make iterate() and iterateReverse() callbacks easier to read
  enum class Iterate { Continue, Break };

  /**
   * Callback when calling iterate() over a const header map.
   * @param header supplies the header entry.
   * @param context supplies the context passed to iterate().
   * @return Iterate::Continue to continue iteration.
   */
  using ConstIterateCb = Iterate (*)(const HeaderEntry&, void*);

  /**
   * Iterate over a constant header map.
   * @param cb supplies the iteration callback.
   * @param context supplies the context that will be passed to the callback.
   */
  virtual void iterate(ConstIterateCb cb, void* context) const PURE;

  /**
   * Iterate over a constant header map in reverse order.
   * @param cb supplies the iteration callback.
   * @param context supplies the context that will be passed to the callback.
   */
  virtual void iterateReverse(ConstIterateCb cb, void* context) const PURE;

  enum class Lookup { Found, NotFound, NotSupported };

  /**
   * Lookup one of the predefined inline headers (see ALL_INLINE_HEADERS below) by key.
   * @param key supplies the header key.
   * @param entry is set to the header entry if it exists and if key is one of the predefined inline
   * headers; otherwise, nullptr.
   * @return Lookup::Found if lookup was successful, Lookup::NotFound if the header entry doesn't
   * exist, or Lookup::NotSupported if key is not one of the predefined inline headers.
   */
  virtual Lookup lookup(const LowerCaseString& key, const HeaderEntry** entry) const PURE;

  /**
   * Remove all instances of a header by key.
   * @param key supplies the header key to remove.
   */
  virtual void remove(const LowerCaseString& key) PURE;

  /**
   * Remove all instances of headers where the key begins with the supplied prefix.
   * @param prefix supplies the prefix to match header keys against.
   */
  virtual void removePrefix(const LowerCaseString& prefix) PURE;

  /**
   * @return the number of headers in the map.
   */
  virtual size_t size() const PURE;

  /**
   * @return true if the map is empty, false otherwise.
   */
  virtual bool empty() const PURE;

  /**
   * Dump the header map to the ostream specified
   *
   * @param os the stream to dump state to
   * @param indent_level the depth, for pretty-printing.
   *
   * This function is called on Envoy fatal errors so should avoid memory allocation where possible.
   */
  virtual void dumpState(std::ostream& os, int indent_level = 0) const PURE;

  /**
   * Allow easy pretty-printing of the key/value pairs in HeaderMap
   * @param os supplies the ostream to print to.
   * @param headers the headers to print.
   */
  friend std::ostream& operator<<(std::ostream& os, const HeaderMap& headers) {
    headers.dumpState(os);
    return os;
  }
};

using HeaderMapPtr = std::unique_ptr<HeaderMap>;

/**
 * Convenient container type for storing Http::LowerCaseString and std::string key/value pairs.
 */
using HeaderVector = std::vector<std::pair<LowerCaseString, std::string>>;

/**
 * An interface to be implemented by header matchers.
 */
class HeaderMatcher {
public:
  virtual ~HeaderMatcher() = default;

  /*
   * Check whether header matcher matches any headers in a given HeaderMap.
   */
  virtual bool matchesHeaders(const HeaderMap& headers) const PURE;
};

using HeaderMatcherSharedPtr = std::shared_ptr<HeaderMatcher>;

} // namespace Http
} // namespace Envoy
