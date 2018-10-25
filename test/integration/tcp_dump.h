#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Envoy {

// RAII tcpdump wrapper for tests. It's expected that tcpdump is in the path.
class TcpDump {
public:
  // Start tcpdump on a given interface, to a given file on a set of given
  // ports.
  TcpDump(const std::string& path, const std::string& iface, const std::vector<uint32_t>& ports);
  // Stop tcpdump and wait for child process termination.
  ~TcpDump();

private:
  int tcpdump_pid_;
};

typedef std::unique_ptr<TcpDump> TcpDumpPtr;

} // namespace Envoy
