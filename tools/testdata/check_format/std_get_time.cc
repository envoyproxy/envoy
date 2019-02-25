#include <stdio>
#include <iomanip>

namespace Envoy {

void parse_time() {
  std::tm t = {};
  std::istringstream ss("2018-December-17 14:38:00");
  ss >> std::get_time(&t, "%Y-%b-%d %H:%M:%S");
  if (ss.fail()) {
    std::cout << "Parse failed\n";
  } else {
    std::cout << std::put_time(&t, "%c") << '\n';
  }
}

} // namespace Envoy
