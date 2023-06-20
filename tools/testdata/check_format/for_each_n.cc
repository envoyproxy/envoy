#include <algorithm>
#include <vector>
namespace Envoy {

void foo() {
  std::vector<int> vec;
  std::for_each_n(vec.begin(), 10, (int){});
}

} // namespace Envoy
