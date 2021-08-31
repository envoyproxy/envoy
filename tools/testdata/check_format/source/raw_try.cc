#include <string>
#include <exception>

namespace Envoy {

struct Try {
  Try(std::string s) {
    try {
      std::stoi(s);
    }
    catch (std::exception&) {}
  }
};

} // namespace Envoy
