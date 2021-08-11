#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/http/header_formatter.h"
#include "envoy/tracing/trace_context.h"

#include "source/common/common/assert.h"
#include "source/common/common/hash.h"
#include "source/common/common/macros.h"

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
  LowerCaseString& operator=(LowerCaseString&& rhs) noexcept {
    string_ = std::move(rhs.string_);
    ASSERT(valid());
    return *this;
  }

  LowerCaseString(const LowerCaseString& rhs) : string_(rhs.string_) { ASSERT(valid()); }
  LowerCaseString& operator=(const LowerCaseString& rhs) {
    string_ = std::move(rhs.string_);
    ASSERT(valid());
    return *this;
  }

  explicit LowerCaseString(absl::string_view new_string) : string_(new_string) {
    ASSERT(valid());
    lower();
  }

  const std::string& get() const { return string_; }
  bool operator==(const LowerCaseString& rhs) const { return string_ == rhs.string_; }
  bool operator!=(const LowerCaseString& rhs) const { return string_ != rhs.string_; }
  bool operator<(const LowerCaseString& rhs) const { return string_.compare(rhs.string_) < 0; }

  friend std::ostream& operator<<(std::ostream& os, const LowerCaseString& lower_case_string) {
    return os << lower_case_string.string_;
  }

  // Implicit conversion to absl::string_view.
  operator absl::string_view() const { return string_; }

private:
  void lower() {
    std::transform(string_.begin(), string_.end(), string_.begin(), absl::ascii_tolower);
  }
  bool valid() const { return validHeaderString(string_); }

  std::string string_;
};

/**
 * Convenient type for a vector of lower case string and string pair.
 */
using LowerCaseStrPairVector =
    std::vector<std::pair<const Http::LowerCaseString, const std::string>>;

/**
 * Convenient type for an inline vector that will be used by HeaderString.
 */
using InlineHeaderVector = absl::InlinedVector<char, 128>;

/**
 * Convenient type for the underlying type of HeaderString that allows a variant
 * between string_view and the InlinedVector.
 */
using VariantHeader = absl::variant<absl::string_view, InlineHeaderVector>;

/**
 * This is a string implementation for use in header processing. It is heavily optimized for
 * performance. It supports 2 different types of storage and can switch between them:
 * 1) A reference.
 * 2) An InlinedVector (an optimized interned string for small strings, but allows heap
 * allocation if needed).
 */
class HeaderString {
public:
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
  explicit HeaderString(absl::string_view ref_value);

  HeaderString(HeaderString&& move_value) noexcept;
  ~HeaderString() = default;

  /**
   * Append data to an existing string. If the string is a reference string the reference data is
   * not copied.
   */
  void append(const char* data, uint32_t size);

  /**
   * Transforms the inlined vector data using the given UnaryOperation (conforms
   * to std::transform).
   * @param unary_op the operations to be performed on each of the elements.
   */
  template <typename UnaryOperation> void inlineTransform(UnaryOperation&& unary_op) {
    ASSERT(type() == Type::Inline);
    std::transform(absl::get<InlineHeaderVector>(buffer_).begin(),
                   absl::get<InlineHeaderVector>(buffer_).end(),
                   absl::get<InlineHeaderVector>(buffer_).begin(), unary_op);
  }

  /**
   * Trim trailing whitespaces from the HeaderString. Only supported by the "Inline" HeaderString
   * representation.
   */
  void rtrim();

  /**
   * Get an absl::string_view. It will NOT be NUL terminated!
   *
   * @return an absl::string_view.
   */
  absl::string_view getStringView() const;

  /**
   * Return the string to a default state. Reference strings are not touched. Both inline/dynamic
   * strings are reset to zero size.
   */
  void clear();

  /**
   * @return whether the string is empty or not.
   */
  bool empty() const { return size() == 0; }

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
  void setReference(absl::string_view ref_value);

  /**
   * @return whether the string is a reference or an InlinedVector.
   */
  bool isReference() const { return type() == Type::Reference; }

  /**
   * @return the size of the string, not including the null terminator.
   */
  uint32_t size() const;

  bool operator==(const char* rhs) const {
    return getStringView() == absl::NullSafeStringView(rhs);
  }
  bool operator==(absl::string_view rhs) const { return getStringView() == rhs; }
  bool operator!=(const char* rhs) const {
    return getStringView() != absl::NullSafeStringView(rhs);
  }
  bool operator!=(absl::string_view rhs) const { return getStringView() != rhs; }

private:
  enum class Type { Reference, Inline };

  VariantHeader buffer_;

  bool valid() const;

  /**
   * @return the type of backing storage for the string.
   */
  Type type() const;
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
 * The following defines all default request headers that Envoy allows direct access to inside of
 * the header map. In practice, these are all headers used during normal Envoy request flow
 * processing. This allows O(1) access to these headers without even a hash lookup.
 *
 */
#define INLINE_REQ_STRING_HEADERS(HEADER_FUNC)                                                     \
  HEADER_FUNC(ClientTraceId)                                                                       \
  HEADER_FUNC(EnvoyDownstreamServiceCluster)                                                       \
  HEADER_FUNC(EnvoyDownstreamServiceNode)                                                          \
  HEADER_FUNC(EnvoyExternalAddress)                                                                \
  HEADER_FUNC(EnvoyForceTrace)                                                                     \
  HEADER_FUNC(EnvoyHedgeOnPerTryTimeout)                                                           \
  HEADER_FUNC(EnvoyInternalRequest)                                                                \
  HEADER_FUNC(EnvoyIpTags)                                                                         \
  HEADER_FUNC(EnvoyRetryOn)                                                                        \
  HEADER_FUNC(EnvoyRetryGrpcOn)                                                                    \
  HEADER_FUNC(EnvoyRetriableStatusCodes)                                                           \
  HEADER_FUNC(EnvoyRetriableHeaderNames)                                                           \
  HEADER_FUNC(EnvoyOriginalPath)                                                                   \
  HEADER_FUNC(EnvoyOriginalUrl)                                                                    \
  HEADER_FUNC(EnvoyUpstreamAltStatName)                                                            \
  HEADER_FUNC(EnvoyUpstreamRequestTimeoutAltResponse)                                              \
  HEADER_FUNC(Expect)                                                                              \
  HEADER_FUNC(ForwardedClientCert)                                                                 \
  HEADER_FUNC(ForwardedFor)                                                                        \
  HEADER_FUNC(ForwardedProto)                                                                      \
  HEADER_FUNC(GrpcTimeout)                                                                         \
  HEADER_FUNC(Host)                                                                                \
  HEADER_FUNC(Method)                                                                              \
  HEADER_FUNC(Path)                                                                                \
  HEADER_FUNC(Protocol)                                                                            \
  HEADER_FUNC(Scheme)                                                                              \
  HEADER_FUNC(TE)                                                                                  \
  HEADER_FUNC(UserAgent)

#define INLINE_REQ_NUMERIC_HEADERS(HEADER_FUNC)                                                    \
  HEADER_FUNC(EnvoyExpectedRequestTimeoutMs)                                                       \
  HEADER_FUNC(EnvoyMaxRetries)                                                                     \
  HEADER_FUNC(EnvoyUpstreamRequestTimeoutMs)                                                       \
  HEADER_FUNC(EnvoyUpstreamRequestPerTryTimeoutMs)

#define INLINE_REQ_HEADERS(HEADER_FUNC)                                                            \
  INLINE_REQ_STRING_HEADERS(HEADER_FUNC)                                                           \
  INLINE_REQ_NUMERIC_HEADERS(HEADER_FUNC)

/**
 * Default O(1) response headers.
 */
#define INLINE_RESP_STRING_HEADERS(HEADER_FUNC)                                                    \
  HEADER_FUNC(Date)                                                                                \
  HEADER_FUNC(EnvoyDegraded)                                                                       \
  HEADER_FUNC(EnvoyImmediateHealthCheckFail)                                                       \
  HEADER_FUNC(EnvoyRateLimited)                                                                    \
  HEADER_FUNC(EnvoyUpstreamCanary)                                                                 \
  HEADER_FUNC(EnvoyUpstreamHealthCheckedCluster)                                                   \
  HEADER_FUNC(Location)                                                                            \
  HEADER_FUNC(Server)

#define INLINE_RESP_NUMERIC_HEADERS(HEADER_FUNC)                                                   \
  HEADER_FUNC(EnvoyUpstreamServiceTime)                                                            \
  HEADER_FUNC(Status)

#define INLINE_RESP_HEADERS(HEADER_FUNC)                                                           \
  INLINE_RESP_STRING_HEADERS(HEADER_FUNC)                                                          \
  INLINE_RESP_NUMERIC_HEADERS(HEADER_FUNC)

/**
 * Default O(1) request and response headers.
 */
#define INLINE_REQ_RESP_STRING_HEADERS(HEADER_FUNC)                                                \
  HEADER_FUNC(Connection)                                                                          \
  HEADER_FUNC(ContentType)                                                                         \
  HEADER_FUNC(EnvoyDecoratorOperation)                                                             \
  HEADER_FUNC(KeepAlive)                                                                           \
  HEADER_FUNC(ProxyConnection)                                                                     \
  HEADER_FUNC(RequestId)                                                                           \
  HEADER_FUNC(TransferEncoding)                                                                    \
  HEADER_FUNC(Upgrade)                                                                             \
  HEADER_FUNC(Via)

#define INLINE_REQ_RESP_NUMERIC_HEADERS(HEADER_FUNC)                                               \
  HEADER_FUNC(ContentLength)                                                                       \
  HEADER_FUNC(EnvoyAttemptCount)

#define INLINE_REQ_RESP_HEADERS(HEADER_FUNC)                                                       \
  INLINE_REQ_RESP_STRING_HEADERS(HEADER_FUNC)                                                      \
  INLINE_REQ_RESP_NUMERIC_HEADERS(HEADER_FUNC)

/**
 * Default O(1) response headers and trailers.
 */
#define INLINE_RESP_STRING_HEADERS_TRAILERS(HEADER_FUNC) HEADER_FUNC(GrpcMessage)

#define INLINE_RESP_NUMERIC_HEADERS_TRAILERS(HEADER_FUNC) HEADER_FUNC(GrpcStatus)

#define INLINE_RESP_HEADERS_TRAILERS(HEADER_FUNC)                                                  \
  INLINE_RESP_STRING_HEADERS_TRAILERS(HEADER_FUNC)                                                 \
  INLINE_RESP_NUMERIC_HEADERS_TRAILERS(HEADER_FUNC)

/**
 * The following functions are defined for each inline header above.

 * E.g., for path we have:
 * Path() -> returns the header entry if it exists or nullptr.
 * removePath() -> removes the header if it exists.
 * setPath(path_string) -> sets the header value to the string path_string by copying the data.
 *
 */
#define DEFINE_INLINE_HEADER(name)                                                                 \
  virtual const HeaderEntry* name() const PURE;                                                    \
  virtual size_t remove##name() PURE;                                                              \
  virtual absl::string_view get##name##Value() const PURE;                                         \
  virtual void set##name(absl::string_view value) PURE;

/*
 * For inline headers that have string values, there are also:
 * appendPath(path, "/") -> appends the string path with delimiter "/" to the header value.
 * setReferencePath(PATH) -> sets header value to reference string PATH.
 *
 */
#define DEFINE_INLINE_STRING_HEADER(name)                                                          \
  DEFINE_INLINE_HEADER(name)                                                                       \
  virtual void append##name(absl::string_view data, absl::string_view delimiter) PURE;             \
  virtual void setReference##name(absl::string_view value) PURE;

/*
 * For inline headers that use integers, there is:
 * setContentLength(5) -> sets the header value to the integer 5.
 */
#define DEFINE_INLINE_NUMERIC_HEADER(name)                                                         \
  DEFINE_INLINE_HEADER(name)                                                                       \
  virtual void set##name(uint64_t) PURE;

/**
 * Wraps a set of HTTP headers.
 */
class HeaderMap {
public:
  virtual ~HeaderMap() = default;

  /**
   * For testing. This is an exact match comparison (order matters).
   */
  virtual bool operator==(const HeaderMap& rhs) const PURE;
  virtual bool operator!=(const HeaderMap& rhs) const PURE;

  /**
   * Add a header via full move. This is the expected high performance paths for codecs populating
   * a map when receiving.
   * @param key supplies the header key.
   * @param value supplies the header value.
   */
  virtual void addViaMove(HeaderString&& key, HeaderString&& value) PURE;

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
  virtual void addReference(const LowerCaseString& key, absl::string_view value) PURE;

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
  virtual void addReferenceKey(const LowerCaseString& key, absl::string_view value) PURE;

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
  virtual void addCopy(const LowerCaseString& key, absl::string_view value) PURE;

  /**
   * Appends data to header. If header already has a value, the string "," is added between the
   * existing value and data.
   *
   * @param key specifies the name of the header to append; it WILL be copied.
   * @param value specifies the value of the header to add; it WILL be copied.
   *
   * Caution: This iterates over the HeaderMap to find the header to append. This will modify only
   * the first occurrence of the header.
   * TODO(asraa): Investigate whether necessary to append to all headers with the key.
   */
  virtual void appendCopy(const LowerCaseString& key, absl::string_view value) PURE;

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
  virtual void setReference(const LowerCaseString& key, absl::string_view value) PURE;

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
  virtual void setReferenceKey(const LowerCaseString& key, absl::string_view value) PURE;

  /**
   * Replaces a header value by copying the value. Copies the key if the key does not exist.
   *
   * Calling setCopy multiple times for the same header will result in only the last header
   * being present in the HeaderMap.
   *
   * @param key specifies the name of the header to set; it WILL be copied.
   * @param value specifies the value of the header to set; it WILL be copied.
   *
   * Caution: This iterates over the HeaderMap to find the header to set. This will modify only the
   * first occurrence of the header.
   * TODO(asraa): Investigate whether necessary to set all headers with the key.
   */
  virtual void setCopy(const LowerCaseString& key, absl::string_view value) PURE;

  /**
   * @return uint64_t the size of the header map in bytes. This is the sum of the header keys and
   * values and does not account for data structure overhead.
   */
  virtual uint64_t byteSize() const PURE;

  /**
   * This is a wrapper for the return result from get(). It avoids a copy when translating from
   * non-const HeaderEntry to const HeaderEntry and only provides const access to the result.
   */
  using NonConstGetResult = absl::InlinedVector<HeaderEntry*, 1>;
  class GetResult {
  public:
    GetResult() = default;
    explicit GetResult(NonConstGetResult&& result) : result_(std::move(result)) {}
    void operator=(GetResult&& rhs) noexcept { result_ = std::move(rhs.result_); }

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
  virtual GetResult get(const LowerCaseString& key) const PURE;

  // aliases to make iterate() and iterateReverse() callbacks easier to read
  enum class Iterate { Continue, Break };

  /**
   * Callback when calling iterate() over a const header map.
   * @param header supplies the header entry.
   * @return Iterate::Continue to continue iteration, or Iterate::Break to stop;
   */
  using ConstIterateCb = std::function<Iterate(const HeaderEntry&)>;

  /**
   * Iterate over a constant header map.
   * @param cb supplies the iteration callback.
   */
  virtual void iterate(ConstIterateCb cb) const PURE;

  /**
   * Iterate over a constant header map in reverse order.
   * @param cb supplies the iteration callback.
   */
  virtual void iterateReverse(ConstIterateCb cb) const PURE;

  /**
   * Clears the headers in the map.
   */
  virtual void clear() PURE;

  /**
   * Remove all instances of a header by key.
   * @param key supplies the header key to remove.
   * @return the number of headers removed.
   */
  virtual size_t remove(const LowerCaseString& key) PURE;

  /**
   * Remove all instances of headers where the header matches the predicate.
   * @param predicate supplies the predicate to match headers against.
   * @return the number of headers removed.
   */
  using HeaderMatchPredicate = std::function<bool(const HeaderEntry&)>;
  virtual size_t removeIf(const HeaderMatchPredicate& predicate) PURE;

  /**
   * Remove all instances of headers where the key begins with the supplied prefix.
   * @param prefix supplies the prefix to match header keys against.
   * @return the number of headers removed.
   */
  virtual size_t removePrefix(const LowerCaseString& prefix) PURE;

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

  /**
   * Return the optional stateful formatter attached to this header map.
   *
   * Filters can use the non-const version to process additional header keys during operation if
   * they wish. The sequence of events would be to first add/modify the header map, and then call
   * processKey(), similar to what is done when headers are received by the codec.
   *
   * TODO(mattklein123): The above sequence will not work for headers added via route (headers to
   * add, etc.). We can potentially add direct processKey() calls in these places as a follow up.
   */
  virtual StatefulHeaderKeyFormatterOptConstRef formatter() const PURE;
  virtual StatefulHeaderKeyFormatterOptRef formatter() PURE;
};

using HeaderMapPtr = std::unique_ptr<HeaderMap>;

/**
 * Wraps a set of header modifications.
 */
struct HeaderTransforms {
  std::vector<std::pair<Http::LowerCaseString, std::string>> headers_to_append;
  std::vector<std::pair<Http::LowerCaseString, std::string>> headers_to_overwrite;
  std::vector<Http::LowerCaseString> headers_to_remove;
};

/**
 * Registry for custom headers. Headers can be registered multiple times in independent
 * compilation units and will still point to the same slot. Headers are registered independently
 * for each concrete header map type and do not overlap. Handles are strongly typed and do not
 * allow mixing.
 */
class CustomInlineHeaderRegistry {
public:
  enum class Type { RequestHeaders, RequestTrailers, ResponseHeaders, ResponseTrailers };
  using RegistrationMap = std::map<LowerCaseString, size_t>;

  // A "phantom" type is used here to force the compiler to verify that handles are not mixed
  // between concrete header map types.
  template <Type type> struct Handle {
    Handle(RegistrationMap::const_iterator it) : it_(it) {}
    bool operator==(const Handle& rhs) const { return it_ == rhs.it_; }

    RegistrationMap::const_iterator it_;
  };

  /**
   * Register an inline header and return a handle for use in inline header calls. Must be called
   * prior to finalize().
   */
  template <Type type>
  static Handle<type> registerInlineHeader(const LowerCaseString& header_name) {
    static size_t inline_header_index = 0;

    ASSERT(!mutableFinalized<type>());
    auto& map = mutableRegistrationMap<type>();
    auto entry = map.find(header_name);
    if (entry == map.end()) {
      map[header_name] = inline_header_index++;
    }
    return Handle<type>(map.find(header_name));
  }

  /**
   * Fetch the handle for a registered inline header. May only be called after finalized().
   */
  template <Type type>
  static absl::optional<Handle<type>> getInlineHeader(const LowerCaseString& header_name) {
    ASSERT(mutableFinalized<type>());
    auto& map = mutableRegistrationMap<type>();
    auto entry = map.find(header_name);
    if (entry != map.end()) {
      return Handle<type>(entry);
    }
    return absl::nullopt;
  }

  /**
   * Fetch all registered headers. May only be called after finalized().
   */
  template <Type type> static const RegistrationMap& headers() {
    ASSERT(mutableFinalized<type>());
    return mutableRegistrationMap<type>();
  }

  /**
   * Finalize the custom header registrations. No further changes are allowed after this point.
   * This guaranteed that all header maps created by the process have the same variable size and
   * custom registrations.
   */
  template <Type type> static void finalize() {
    ASSERT(!mutableFinalized<type>());
    mutableFinalized<type>() = true;
  }

private:
  template <Type type> static RegistrationMap& mutableRegistrationMap() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(RegistrationMap);
  }
  template <Type type> static bool& mutableFinalized() { MUTABLE_CONSTRUCT_ON_FIRST_USE(bool); }
};

/**
 * Static initializer to register a custom header in a compilation unit. This can be used by
 * extensions to register custom headers.
 */
template <CustomInlineHeaderRegistry::Type type> class RegisterCustomInlineHeader {
public:
  RegisterCustomInlineHeader(const LowerCaseString& header)
      : handle_(CustomInlineHeaderRegistry::registerInlineHeader<type>(header)) {}

  typename CustomInlineHeaderRegistry::Handle<type> handle() { return handle_; }

private:
  const typename CustomInlineHeaderRegistry::Handle<type> handle_;
};

/**
 * The following functions allow O(1) access for custom inline headers.
 */
template <CustomInlineHeaderRegistry::Type type> class CustomInlineHeaderBase {
public:
  virtual ~CustomInlineHeaderBase() = default;

  static constexpr CustomInlineHeaderRegistry::Type header_map_type = type;
  using Handle = CustomInlineHeaderRegistry::Handle<header_map_type>;

  virtual const HeaderEntry* getInline(Handle handle) const PURE;
  virtual void appendInline(Handle handle, absl::string_view data,
                            absl::string_view delimiter) PURE;
  virtual void setReferenceInline(Handle, absl::string_view value) PURE;
  virtual void setInline(Handle, absl::string_view value) PURE;
  virtual void setInline(Handle, uint64_t value) PURE;
  virtual size_t removeInline(Handle handle) PURE;
  absl::string_view getInlineValue(Handle handle) const {
    const auto header = getInline(handle);
    if (header != nullptr) {
      return header->value().getStringView();
    }
    return {};
  }
};

/**
 * Typed derived classes for all header map types.
 */

// Base class for both request and response headers.
class RequestOrResponseHeaderMap : public HeaderMap {
public:
  INLINE_REQ_RESP_STRING_HEADERS(DEFINE_INLINE_STRING_HEADER)
  INLINE_REQ_RESP_NUMERIC_HEADERS(DEFINE_INLINE_NUMERIC_HEADER)
};

// Request headers.
class RequestHeaderMap
    : public RequestOrResponseHeaderMap,
      public CustomInlineHeaderBase<CustomInlineHeaderRegistry::Type::RequestHeaders>,
      public Tracing::TraceContext {
public:
  INLINE_REQ_STRING_HEADERS(DEFINE_INLINE_STRING_HEADER)
  INLINE_REQ_NUMERIC_HEADERS(DEFINE_INLINE_NUMERIC_HEADER)
};
using RequestHeaderMapPtr = std::unique_ptr<RequestHeaderMap>;
using RequestHeaderMapOptRef = OptRef<RequestHeaderMap>;
using RequestHeaderMapOptConstRef = OptRef<const RequestHeaderMap>;

// Request trailers.
class RequestTrailerMap
    : public HeaderMap,
      public CustomInlineHeaderBase<CustomInlineHeaderRegistry::Type::RequestTrailers> {};
using RequestTrailerMapPtr = std::unique_ptr<RequestTrailerMap>;
using RequestTrailerMapOptRef = OptRef<RequestTrailerMap>;
using RequestTrailerMapOptConstRef = OptRef<const RequestTrailerMap>;

// Base class for both response headers and trailers.
class ResponseHeaderOrTrailerMap {
public:
  virtual ~ResponseHeaderOrTrailerMap() = default;

  INLINE_RESP_STRING_HEADERS_TRAILERS(DEFINE_INLINE_STRING_HEADER)
  INLINE_RESP_NUMERIC_HEADERS_TRAILERS(DEFINE_INLINE_NUMERIC_HEADER)
};

// Response headers.
class ResponseHeaderMap
    : public RequestOrResponseHeaderMap,
      public ResponseHeaderOrTrailerMap,
      public CustomInlineHeaderBase<CustomInlineHeaderRegistry::Type::ResponseHeaders> {
public:
  INLINE_RESP_STRING_HEADERS(DEFINE_INLINE_STRING_HEADER)
  INLINE_RESP_NUMERIC_HEADERS(DEFINE_INLINE_NUMERIC_HEADER)
};
using ResponseHeaderMapPtr = std::unique_ptr<ResponseHeaderMap>;
using ResponseHeaderMapOptRef = OptRef<ResponseHeaderMap>;
using ResponseHeaderMapOptConstRef = OptRef<const ResponseHeaderMap>;

// Response trailers.
class ResponseTrailerMap
    : public ResponseHeaderOrTrailerMap,
      public HeaderMap,
      public CustomInlineHeaderBase<CustomInlineHeaderRegistry::Type::ResponseTrailers> {};
using ResponseTrailerMapPtr = std::unique_ptr<ResponseTrailerMap>;
using ResponseTrailerMapOptRef = OptRef<ResponseTrailerMap>;
using ResponseTrailerMapOptConstRef = OptRef<const ResponseTrailerMap>;

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

  /**
   * Check whether header matcher matches any headers in a given HeaderMap.
   */
  virtual bool matchesHeaders(const HeaderMap& headers) const PURE;
};

using HeaderMatcherSharedPtr = std::shared_ptr<HeaderMatcher>;

} // namespace Http
} // namespace Envoy
