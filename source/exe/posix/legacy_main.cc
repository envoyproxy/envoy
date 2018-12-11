#include "common/common/thread_impl.h"

#include "exe/legacy_main.h"
#include "exe/main_common.h"

namespace Envoy {

int main_common(OptionsImpl& options) {
  try {
    Event::RealTimeSystem real_time_system_;
    DefaultTestHooks default_test_hooks_;
    ProdComponentFactory prod_component_factory_;
    Thread::ThreadFactoryImplPosix thread_factory_;
    MainCommonBase main_common(options, real_time_system_, default_test_hooks_,
                               prod_component_factory_,
                               std::make_unique<Runtime::RandomGeneratorImpl>(), thread_factory_);
    return main_common.run() ? EXIT_SUCCESS : EXIT_FAILURE;
  } catch (EnvoyException& e) {
    return EXIT_FAILURE;
  }
}

} // namespace Envoy
