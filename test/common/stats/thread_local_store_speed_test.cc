// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"
#include "common/thread_local/thread_local_impl.h"
#include "common/stats/heap_stat_data.h"
#include "common/stats/stats_options_impl.h"
#include "common/stats/thread_local_store.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/exe/main_common_test_base.h"
#include "test/test_common/simulated_time_system.h"

#include "testing/base/public/benchmark.h"

namespace Envoy {

/*struct Worker {
  Worker(Event::TimeSystem& time_system, ThreadLocal::InstanceImpl& tls,
         const std::function<void()>& f)
      : dispatcher_(time_system),
        function_(f) {
    tls.registerThread(dispatcher_, false);
   }

  ~Worker() {
    thread_->join();
  }

  void start() {
    thread_ = std::make_unique<Thread::Thread>(function_);
  }

  Event::DispatcherImpl dispatcher_;
  std::function<void()> function_;
  std::unique_ptr<Thread::Thread> thread_;
};
*/

class ThreadLocalStorePerf {
 public:
  ThreadLocalStorePerf() : store_(options_, heap_alloc_) {}

  ~ThreadLocalStorePerf() {
    store_.shutdownThreading();
    if (tls_.get() != nullptr) {
      tls_->shutdownGlobalThreading();
    }
  }

  void accessCounters() {
    Stats::TestUtil::forEachSampleStat(
        1000, [this](absl::string_view name) { store_.counter(std::string(name)); });
  }

  void initThreading() {
    dispatcher_ = std::make_unique<Event::DispatcherImpl>(time_system_);
    tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
    store_.initializeThreading(*dispatcher_, *tls_);
  }

  std::unique_ptr<Worker> makeWorker(const std::function<void()>& f) {
    return std::make_unique<Worker>(time_system_, *tls_, f);
  }


 private:
  Stats::StatsOptionsImpl options_;
  Event::SimulatedTimeSystem time_system_;
  Stats::HeapStatDataAllocator heap_alloc_;
  std::unique_ptr<Event::DispatcherImpl> dispatcher_;
  std::unique_ptr<ThreadLocal::InstanceImpl> tls_;
  Stats::ThreadLocalStoreImpl store_;
};

} // namespace Envoy

static void BM_StatsNoTls(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;
  for (auto _ : state) {
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsNoTls);

static void BM_StatsWithTls(benchmark::State& state) {
  MainCommonTestBase test_base;
  test_base.addArg("--disable-hot-restart");


  constexpr char bootstrap[] = R"(
node {
  id: "test_id"
  cluster: "test_cluster"
  metadata {
    fields {
      key: "test_key"
      value {
        string_value: "test_value"
      }
    }
  }
  locality {
    sub_zone: "test_sub_zone"
  }
}
admin {
  access_log_path: "/dev/null"
  address {
    socket_address {
      address: "::"
      port_value: 0
    }
  }
}
)";

  static constexpr char* argv[] = { "--disable-hot-restart",

  MainCommon main_common(argc(), argv());
  Envoy::MainCommonBase(OptionsImpl& options, Event::TimeSystem& time_system, TestHooks& test_hooks,
                 Server::ComponentFactory& component_factory,
                 std::unique_ptr<Runtime::RandomGenerator>&& random_generator);

  Envoy::Thread::MutexBasicLockable log_lock;
  Envoy::Logger::Context logging_context(spdlog::level::info,
                                         Envoy::Logger::Logger::DEFAULT_LOG_FORMAT,
                                         log_lock);
  Envoy::ThreadLocalStorePerf context;
  context.initThreading();

  const int num_threads = 1;

  std::vector<std::unique_ptr<Envoy::Worker>> workers;
  for (int i = 0; i < num_threads; ++i) {
    workers.push_back(context.makeWorker([&state, &context]() {
                                           for (auto _ : state) {
                                             context.accessCounters();
                                           }
                                         }));
  }
  for (auto& worker : workers) {
    worker->start();
  }
}
BENCHMARK(BM_StatsWithTls);


// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);

  Envoy::Event::Libevent::Global::initialize();
echo "admin:
  access_log_path: /dev/null
  address:
    socket_address: { address: 127.0.0.1, port_value: 9901 }

static_resources:
  listeners:
  - name: listener
    address:
      socket_address: { address: 0.0.0.0, port_value: 40000 }
    filter_chains:
    - filters:
      - name: envoy.http_connection_manager
        config:
          stat_prefix: ingress_http
          route_config:
            name: local_route
            virtual_hosts:
            - name: local_service
              domains: \*
              routes:
              - match: { prefix: / }
                route: { cluster: service_0000 }
          http_filters:
          - name: envoy.router
  clusters:"

for i in `seq -f "%04g" 0 $(($1-1))`; do
  echo "
  - name: service_$i
    connect_timeout: 0.25s
    type: STATIC
    dns_lookup_family: V4_ONLY
    lb_policy: ROUND_ROBIN
    hosts: [{ socket_address: { address: 127.0.0.1, port_value: 60000 }}]"
done

  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
