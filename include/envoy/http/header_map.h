#pragma once

#include "envoy/common/pure.h"

namespace Http {

/**
 * Wrapper for a lower case string used in header operations to generally avoid needless case
 * insensitive compares.
 */
class LowerCaseString {
public:
  explicit LowerCaseString(LowerCaseString&& rhs) : string_(std::move(rhs.string_)) {}
  explicit LowerCaseString(const LowerCaseString& rhs) : string_(rhs.string_) {}
  explicit LowerCaseString(const std::string& new_string) : string_(new_string) { lower(); }
  explicit LowerCaseString(std::string&& new_string, bool convert = true)
      : string_(std::move(new_string)) {
    if (convert) {
      lower();
    }
  }

  LowerCaseString(const char* new_string) : string_(new_string) { lower(); }

  const std::string& get() const { return string_; }
  bool operator==(const LowerCaseString& rhs) const { return string_ == rhs.string_; }

private:
  void lower() { std::transform(string_.begin(), string_.end(), string_.begin(), tolower); }

  std::string string_;
};

/**
 * Wraps a set of HTTP headers.
 */
class HeaderMap {
public:
  virtual ~HeaderMap() {}

  /**
   * Copy a key/value into the map.
   * @param key supplies the key to copy in.
   * @param value supplies the value to copy in.
   */
  virtual void addViaCopy(const LowerCaseString& key, const std::string& value) PURE;

  /**
   * Move a key/value into the map.
   * @param key supplies the key to move in.
   * @param value supplies the value to move in.
   */
  virtual void addViaMove(LowerCaseString&& key, std::string&& value) PURE;

  /**
   * Copy a key and move a value into the map.
   * @param key supplies the key to copy in.
   * @param value supplies the value to move in.
   */
  virtual void addViaMoveValue(const LowerCaseString& key, std::string&& value) PURE;

  /**
   * @return uint64_t the approximate size of the header map in bytes.
   */
  virtual uint64_t byteSize() const PURE;

  /**
   * Get a header value by key.
   * @param key supplies the header key.
   * @return the header value or the empty string if the header has no value or does not exist.
   */
  virtual const std::string& get(const LowerCaseString& key) const PURE;

  /**
   * @return whether the map has a specific header (even if it contains an empty value).
   */
  virtual bool has(const LowerCaseString& key) const PURE;

  /**
   * Callback when calling iterate() over a const header map.
   * @param key supplies the header key.
   * @param value supplies header value.
   */
  typedef std::function<void(const LowerCaseString& key, const std::string& value)> ConstIterateCb;

  /**
   * Iterate over a constant header map.
   * @param cb supplies the iteration callback.
   */
  virtual void iterate(ConstIterateCb cb) const PURE;

  /**
   * Replace all instances of a header key with a single copied key/value pair.
   * @param key supplies the header key to replace via copy.
   * @param value supplies the value to replace via copy.
   */
  virtual void replaceViaCopy(const LowerCaseString& key, const std::string& value) PURE;

  /**
   * Replace all instances of a header key with a single moved key/value pair.
   * @param key supplies the header key to replace via move.
   * @param value supplies the header value to replace via move.
   */
  virtual void replaceViaMove(LowerCaseString&& key, std::string&& value) PURE;

  /**
   * Replace all instances of a header key with a single copied header key and a moved value.
   * @param key supplies the header key to replace via copy.
   * @param value supplies the header value to replace via move.
   */
  virtual void replaceViaMoveValue(const LowerCaseString& key, std::string&& value) PURE;

  /**
   * Remove all instances of a header by key.
   * @param key supplies the header key to remove.
   */
  virtual void remove(const LowerCaseString& remove_key) PURE;

  /**
   * @return uint64_t the number of headers currently in the map.
   */
  virtual uint64_t size() const PURE;
};

typedef std::unique_ptr<HeaderMap> HeaderMapPtr;

} // Http
