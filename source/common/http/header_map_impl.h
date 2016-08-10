#pragma once

#include "envoy/http/header_map.h"

namespace Http {

/**
 * Implementation of Http::HeaderMap. This implementation uses a linked list to keep the headers
 * in order. If it's proven that having fast lookup on top is helpful we can add a map referencing
 * capability as well.
 */
class HeaderMapImpl : public HeaderMap {
public:
  typedef std::pair<LowerCaseString, std::string> HeaderEntry;

  HeaderMapImpl() {}
  HeaderMapImpl(const std::initializer_list<HeaderEntry>& values) : headers_(values) {}
  HeaderMapImpl(const HeaderMap& rhs);

  /**
   * For testing. Equality is based on equality of the backing list.
   */
  bool operator==(const HeaderMapImpl& rhs) const { return headers_ == rhs.headers_; }

  // Http::HeaderMap
  void addViaCopy(const LowerCaseString& key, const std::string& value) override;
  void addViaMove(LowerCaseString&& key, std::string&& value) override;
  void addViaMoveValue(const LowerCaseString& key, std::string&& value) override;
  uint64_t byteSize() const override;
  const std::string& get(const LowerCaseString& key) const override;
  bool has(const LowerCaseString& key) const;
  void iterate(ConstIterateCb cb) const override;
  void replaceViaCopy(const LowerCaseString& key, const std::string& value) override;
  void replaceViaMove(LowerCaseString&& key, std::string&& value) override;
  void replaceViaMoveValue(const LowerCaseString& key, std::string&& value) override;
  void remove(const LowerCaseString& key) override;
  uint64_t size() const override { return headers_.size(); }

private:
  std::list<HeaderEntry> headers_;
};

} // Http
