#pragma once

#include "envoy/common/pure.h"

namespace Http {

/**
 * Wrapper for a lower case string used in header operations to generally avoid needless case
 * insensitive compares.
 */
class LowerCaseString {
public:
  LowerCaseString(LowerCaseString&& rhs) : string_(std::move(rhs.string_)) {}
  LowerCaseString(const LowerCaseString& rhs) : string_(rhs.string_) {}
  explicit LowerCaseString(const std::string& new_string) : string_(new_string) { lower(); }
  explicit LowerCaseString(std::string&& new_string, bool convert = true)
      : string_(std::move(new_string)) {
    if (convert) {
      lower();
    }
  }

  const std::string& get() const { return string_; }
  bool operator==(const LowerCaseString& rhs) const { return string_ == rhs.string_; }

private:
  void lower() { std::transform(string_.begin(), string_.end(), string_.begin(), tolower); }

  std::string string_;
};

/**
 * This is a string implementation for use in header processing. It is heavily optimized for
 * performance. It supports 3 different types of storage and can switch between them:
 * 1) A static reference.
 * 2) Interned string.
 * 3) Heap allocated storage.
 */
class HeaderString {
public:
  enum class Type { Inline, Static, Dynamic };

  /**
   * Default constructor. Sets up for inline storage.
   */
  HeaderString();

  /**
   * Constructor for a static string reference.
   * @param static_value MUST point to static data.
   */
  explicit HeaderString(const LowerCaseString& static_value);

  /**
   * Constructor for a static string reference.
   * @param static_value MUST point to static data.
   */
  explicit HeaderString(const std::string& static_value);

  HeaderString(HeaderString&& move_value);
  ~HeaderString();

  /**
   * Append data to an existing string. If the string is a static string the static data is not
   * copied.
   */
  void append(const char* data, uint32_t size);

  /**
   * @return the modifiable backing buffer (either inline or heap allocated).
   */
  char* buffer() { return buffer_.dynamic_; }

  /**
   * @return a null terminated C string.
   */
  const char* c_str() const { return buffer_.static_; }

  /**
   * Return the string to a default state. Static strings are not touched. Both inline/dynamic
   * strings are reset to zero size.
   */
  void clear();

  /**
   * @return whether the string is empty or not.
   */
  bool empty() const { return string_length_ == 0; }

  /**
   * @return whether a substring exists in the string.
   */
  bool find(const char* str) const { return strstr(c_str(), str); }

  /**
   * Set the value of the string by copying data into it. This overwrites any existing string.
   */
  void setCopy(const char* data, uint32_t size);

  /**
   * Set the value of the string to an integer. This overwrites any existing string.
   */
  void setInteger(uint64_t value);

  /**
   * @return the size of the string, not including the null terminator.
   */
  uint32_t size() const { return string_length_; }

  /**
   * @return the type of backing storage for the string.
   */
  Type type() const { return type_; }

  bool operator==(const char* rhs) const { return 0 == strcmp(c_str(), rhs); }
  bool operator!=(const char* rhs) const { return 0 != strcmp(c_str(), rhs); }

private:
  union {
    char* dynamic_;
    const char* static_;
  } buffer_;

  union {
    char inline_buffer_[128];
    uint32_t dynamic_capacity_;
  };

  uint32_t string_length_;
  Type type_;
};

/**
 * Encapsulates an individual header entry (including both key and value).
 */
class HeaderEntry {
public:
  virtual ~HeaderEntry() {}

  /**
   * @return the header key.
   */
  virtual const HeaderString& key() const PURE;

  /**
   * Set the header value by copying data into it.
   */
  virtual void value(const char* value, uint32_t size) PURE;

  /**
   * Set the header value by copying data into it.
   */
  virtual void value(const std::string& value) PURE;

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

private:
  void value(const char*); // Do not allow auto conversion to std::string
};

/**
 * The following defines all headers that Envoy allows direct access to inside of the header map.
 * In practice, these are all headers used during normal Envoy request flow processing. This allows
 * O(1) access to these headers without even a hash lookup.
 */
#define ALL_INLINE_HEADERS(HEADER_FUNC)                                                            \
  HEADER_FUNC(Authorization)                                                                       \
  HEADER_FUNC(ClientTraceId)                                                                       \
  HEADER_FUNC(Connection)                                                                          \
  HEADER_FUNC(ContentLength)                                                                       \
  HEADER_FUNC(ContentType)                                                                         \
  HEADER_FUNC(Date)                                                                                \
  HEADER_FUNC(EnvoyDownstreamServiceCluster)                                                       \
  HEADER_FUNC(EnvoyExpectedRequestTimeoutMs)                                                       \
  HEADER_FUNC(EnvoyExternalAddress)                                                                \
  HEADER_FUNC(EnvoyForceTrace)                                                                     \
  HEADER_FUNC(EnvoyInternalRequest)                                                                \
  HEADER_FUNC(EnvoyMaxRetries)                                                                     \
  HEADER_FUNC(EnvoyOriginalPath)                                                                   \
  HEADER_FUNC(EnvoyRetryOn)                                                                        \
  HEADER_FUNC(EnvoyUpstreamAltStatName)                                                            \
  HEADER_FUNC(EnvoyUpstreamCanary)                                                                 \
  HEADER_FUNC(EnvoyUpstreamHealthCheckedCluster)                                                   \
  HEADER_FUNC(EnvoyUpstreamRequestPerTryTimeoutMs)                                                 \
  HEADER_FUNC(EnvoyUpstreamRequestTimeoutMs)                                                       \
  HEADER_FUNC(EnvoyUpstreamRequestTimeoutAltResponse)                                              \
  HEADER_FUNC(EnvoyUpstreamServiceTime)                                                            \
  HEADER_FUNC(Expect)                                                                              \
  HEADER_FUNC(ForwardedFor)                                                                        \
  HEADER_FUNC(ForwardedProto)                                                                      \
  HEADER_FUNC(GrpcMessage)                                                                         \
  HEADER_FUNC(GrpcStatus)                                                                          \
  HEADER_FUNC(Host)                                                                                \
  HEADER_FUNC(KeepAlive)                                                                           \
  HEADER_FUNC(Method)                                                                              \
  HEADER_FUNC(Path)                                                                                \
  HEADER_FUNC(ProxyConnection)                                                                     \
  HEADER_FUNC(RequestId)                                                                           \
  HEADER_FUNC(Scheme)                                                                              \
  HEADER_FUNC(Server)                                                                              \
  HEADER_FUNC(Status)                                                                              \
  HEADER_FUNC(TransferEncoding)                                                                    \
  HEADER_FUNC(Upgrade)                                                                             \
  HEADER_FUNC(UserAgent)

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
  virtual ~HeaderMap() {}

  ALL_INLINE_HEADERS(DEFINE_INLINE_HEADER)

  /**
   * Add a fully static header to the map. Both key and value MUST point to fully static data.
   * Nothing will be copied.
   */
  virtual void addStatic(const LowerCaseString& key, const std::string& value) PURE;

  /**
   * Add a header with a static key to the map. The key MUST point to fully static data. The value
   * will be copied.
   */
  virtual void addStaticKey(const LowerCaseString& key, uint64_t value) PURE;

  /**
   * Add a header with a static key to the map. The key MUST point to fully static data. The value
   * will be copied.
   */
  virtual void addStaticKey(const LowerCaseString& key, const std::string& value) PURE;

  /**
   * @return uint64_t the approximate size of the header map in bytes.
   */
  virtual uint64_t byteSize() const PURE;

  /**
   * Get a header by key.
   * @param key supplies the header key.
   * @return the header entry if it exsits otherwise nullptr.
   */
  virtual const HeaderEntry* get(const LowerCaseString& key) const PURE;

  /**
   * Callback when calling iterate() over a const header map.
   * @param header supplies the header entry.
   * @param context supplies the context passed to iterate().
   */
  typedef void (*ConstIterateCb)(const HeaderEntry& header, void* context);

  /**
   * Iterate over a constant header map.
   * @param cb supplies the iteration callback.
   * @param context supplies the context that will be passed to the callback.
   */
  virtual void iterate(ConstIterateCb cb, void* context) const PURE;

  /**
   * Remove all instances of a header by key.
   * @param key supplies the header key to remove.
   */
  virtual void remove(const LowerCaseString& key) PURE;

  /**
   * @return the number of headers in the map.
   */
  virtual size_t size() const PURE;
};

typedef std::unique_ptr<HeaderMap> HeaderMapPtr;

} // Http
