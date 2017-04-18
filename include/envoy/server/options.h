#pragma once

#include "envoy/common/pure.h"

namespace Server {

/**
 * General options for the server.
 */
class Options {
public:
  virtual ~Options() {}

  /**
   * @return uint64_t the base ID for the server. This is required for system-wide things like
   *         shared memory, domain sockets, etc. that are used during hot restart. Setting the
   *         base ID to a different value will allow the server to run multiple times on the same
   *         host if desired.
   */
  virtual uint64_t baseId() PURE;

  /**
   * @return the number of worker threads to run in the server.
   */
  virtual uint32_t concurrency() PURE;

  /**
   * @return the number of seconds that envoy will perform draining during a hot restart.
   */
  virtual std::chrono::seconds drainTime() PURE;

  /**
   * @return const std::string& the path to the configuration file.
   */
  virtual const std::string& configPath() PURE;

  /**
   * @return const std::string& the admin address output file.
   */
  virtual const std::string& adminAddressPath() PURE;

  /**
   * @return spdlog::level::level_enum the default log level for the server.
   */
  virtual spdlog::level::level_enum logLevel() PURE;

  /**
   * @return the number of seconds that envoy will wait before shutting down the parent envoy during
   *         a host restart. Generally this will be longer than the drainTime() option.
   */
  virtual std::chrono::seconds parentShutdownTime() PURE;

  /**
   * @return the restart epoch. 0 indicates the first server start, 1 the second, and so on.
   */
  virtual uint64_t restartEpoch() PURE;

  /**
    * @return std::chrono::milliseconds the duration in msec between log flushes.
    */
  virtual std::chrono::milliseconds fileFlushIntervalMsec() PURE;
};

} // Server
