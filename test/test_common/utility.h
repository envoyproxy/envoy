#pragma once

#include "envoy/network/address.h"

#include "common/http/header_map_impl.h"

class TestUtility {
public:
  /**
   * Compare 2 buffers.
   * @param lhs supplies buffer 1.
   * @param rhs supplies buffer 2.
   * @return TRUE if the buffers are equal, false if not.
   */
  static bool buffersEqual(const Buffer::Instance& lhs, const Buffer::Instance& rhs);

  /**
   * Convert a buffer to a string.
   * @param buffer supplies the buffer to convert.
   * @return std::string the converted string.
   */
  static std::string bufferToString(const Buffer::Instance& buffer);

  /**
   * Convert a string list of IP addresses into a list of network addresses usable for DNS
   * response testing.
   */
  static std::list<Network::Address::InstancePtr>
  makeDnsResponse(const std::list<std::string>& addresses);
};

namespace Http {

/**
 * A test version of HeaderMapImpl that adds some niceties since the prod one makes it very
 * difficult to do any string copies without really meaning to.
 */
class TestHeaderMapImpl : public HeaderMapImpl {
public:
  TestHeaderMapImpl();
  TestHeaderMapImpl(const std::initializer_list<std::pair<std::string, std::string>>& values);
  TestHeaderMapImpl(const HeaderMap& rhs);

  void addViaCopy(const std::string& key, const std::string& value);
  void addViaCopy(const LowerCaseString& key, const std::string& value);
  std::string get_(const std::string& key);
  std::string get_(const LowerCaseString& key);
  bool has(const std::string& key);
  bool has(const LowerCaseString& key);
};

} // Http
