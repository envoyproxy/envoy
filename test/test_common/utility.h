#pragma once

#include <condition_variable>
#include <list>
#include <mutex>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/network/address.h"

#include "common/http/header_map_impl.h"

#include "test/test_common/printers.h"

#include "gtest/gtest.h"

namespace Envoy {
#define EXPECT_THROW_WITH_MESSAGE(statement, expected_exception, message)                          \
  try {                                                                                            \
    statement;                                                                                     \
    ADD_FAILURE() << "Exception should take place. It did not.";                                   \
  } catch (expected_exception & e) {                                                               \
    EXPECT_EQ(message, std::string(e.what()));                                                     \
  }

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
  static std::list<Network::Address::InstanceConstSharedPtr>
  makeDnsResponse(const std::list<std::string>& addresses);

  /**
   * List files in a given directory path
   *
   * @param path directory path to list
   * @param recursive whether or not to traverse subdirectories
   * @return std::vector<std::string> filenames
   */
  static std::vector<std::string> listFiles(const std::string& path, bool recursive);
};

/**
 * This utility class wraps the common case of having a cross-thread "one shot" ready condition.
 */
class ConditionalInitializer {
public:
  /**
   * Set the conditional to ready, should only be called once.
   */
  void setReady();

  /**
   * Block until the conditional is ready, will return immediately if it is already ready.
   *
   */
  void waitReady();

private:
  std::condition_variable cv_;
  std::mutex mutex_;
  bool ready_{false};
};

class ScopedFdCloser {
public:
  ScopedFdCloser(int fd);
  ~ScopedFdCloser();

private:
  int fd_;
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
} // Envoy
