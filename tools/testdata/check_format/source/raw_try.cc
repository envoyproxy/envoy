#include <exception>
#include <string>

namespace Envoy {

struct Try {
  Try(std::string s) {
    try {
      std::stoi(s);
    } catch (std::exception&) {
    }
  }
};

} // namespace Envoy
