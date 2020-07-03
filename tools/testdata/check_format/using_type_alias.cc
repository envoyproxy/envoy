#include <memory>

namespace Envoy {
namespace Network {
class Connection;

using ConnectionPtr = std::unique_ptr<Connection>;

template <class C> using EdfSchedulerPtr = std::unique_ptr<EdfScheduler<C>>;

class A {
  using ConnectionSharedPtr = std::shared_ptr<Connection>;
  using ConnectionOptRef = absl::optional<std::reference_wrapper<Connection>>;
};
} // namespace Network
} // namespace Envoy
