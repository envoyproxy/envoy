#include "test/integration/tcp_dump.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fstream>

#include "common/common/assert.h"
#include "common/common/fmt.h"

namespace Envoy {

TcpDump::TcpDump(const std::string& path, const std::string& iface,
                 const std::vector<uint32_t> ports) {
  // Remove any extant pcap file.
  unlink(path.c_str());
  // Derive the port filter expression.
  std::string port_expr;
  for (uint32_t port : ports) {
    if (!port_expr.empty()) {
      port_expr += " or ";
    }
    port_expr += "tcp port " + std::to_string(port);
  }
  ENVOY_LOG_MISC(debug, "tcpdumping iface {} to {} with filter \"{}\"", iface, path, port_expr);
  // Fork a child process. We use explicit fork/wait over popen/pclose to gain
  // the ability to send signals to the pid.
  tcpdump_pid_ = fork();
  RELEASE_ASSERT(tcpdump_pid_ >= 0, "");
  // execlp in the child process.
  if (tcpdump_pid_ == 0) {
    const int rc = execlp("tcpdump", "tcpdump", "-i", iface.c_str(), "-w", path.c_str(),
                          "--immediate-mode", port_expr.c_str(), nullptr);
    if (rc == -1) {
      perror("tcpdump");
      RELEASE_ASSERT(rc != -1, "Unable to start tcpdump");
    }
  }
  // Wait in parent process until tcpdump is running and has created the pcap
  // file.
  while (true) {
    std::ifstream test_file{path};
    if (test_file.good()) {
      break;
    }
    // Give 50ms sleep.
    ::usleep(50000);
  }
}

TcpDump::~TcpDump() {
  RELEASE_ASSERT(kill(tcpdump_pid_, SIGINT) == 0, "");
  int status;
  RELEASE_ASSERT(waitpid(tcpdump_pid_, &status, 0) != -1, "");
  RELEASE_ASSERT(WEXITSTATUS(status) == 0, "");
  ENVOY_LOG_MISC(debug, "tcpdump terminated");
}

} // namespace Envoy
