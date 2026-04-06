// NOLINT(namespace-envoy)
// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <list>
#include <memory>
#include <vector>

#include "source/common/common/linked_object.h"

#include "benchmark/benchmark.h"

namespace Envoy {

struct StdListObject : public LinkedObject<StdListObject> {
  explicit StdListObject(int v = 0) : value(v) {}
  int value;
};

struct IntrusiveObject : public IntrusiveListNode<IntrusiveObject> {
  explicit IntrusiveObject(int v = 0) : value(v) {}
  int value;
};

// Benchmark inserting N items at the front of a std::list<unique_ptr<T>>.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StdListPushFront(benchmark::State& state) {
  const int n = state.range(0);
  for (auto _ : state) {
    std::list<std::unique_ptr<StdListObject>> list;
    for (int i = 0; i < n; ++i) {
      LinkedList::moveIntoList(std::make_unique<StdListObject>(i), list);
    }
    benchmark::DoNotOptimize(list);
  }
}
BENCHMARK(BM_StdListPushFront)->Arg(64)->Arg(512);

// Benchmark removing every item from a pre-populated std::list<unique_ptr<T>>.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StdListRemoveAll(benchmark::State& state) {
  const int n = state.range(0);
  for (auto _ : state) {
    state.PauseTiming();
    std::list<std::unique_ptr<StdListObject>> list;
    std::vector<StdListObject*> ptrs;
    ptrs.reserve(n);
    for (int i = 0; i < n; ++i) {
      auto obj = std::make_unique<StdListObject>(i);
      ptrs.push_back(obj.get());
      LinkedList::moveIntoListBack(std::move(obj), list);
    }
    state.ResumeTiming();

    for (StdListObject* p : ptrs) {
      benchmark::DoNotOptimize(p->removeFromList(list));
    }
  }
}
BENCHMARK(BM_StdListRemoveAll)->Arg(64)->Arg(512);

// Benchmark iterating over all items in a std::list<unique_ptr<T>> and summing values.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StdListIterate(benchmark::State& state) {
  const int n = state.range(0);
  std::list<std::unique_ptr<StdListObject>> list;
  for (int i = 0; i < n; ++i) {
    LinkedList::moveIntoListBack(std::make_unique<StdListObject>(i), list);
  }
  for (auto _ : state) {
    int64_t sum = 0;
    for (const auto& obj : list) {
      sum += obj->value;
    }
    benchmark::DoNotOptimize(sum);
  }
}
BENCHMARK(BM_StdListIterate)->Arg(64)->Arg(512);

// ---------------------------------------------------------------------------
// IntrusiveList benchmarks
// ---------------------------------------------------------------------------

// Benchmark inserting N items at the front of an IntrusiveList<T>.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_IntrusiveListPushFront(benchmark::State& state) {
  const int n = state.range(0);
  for (auto _ : state) {
    IntrusiveList<IntrusiveObject> list;
    for (int i = 0; i < n; ++i) {
      list.push(std::make_unique<IntrusiveObject>(i));
    }
    benchmark::DoNotOptimize(list);
  }
}
BENCHMARK(BM_IntrusiveListPushFront)->Arg(64)->Arg(512);

// Benchmark removing every item from a pre-populated IntrusiveList<T>.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_IntrusiveListRemoveAll(benchmark::State& state) {
  const int n = state.range(0);
  for (auto _ : state) {
    state.PauseTiming();
    IntrusiveList<IntrusiveObject> list;
    std::vector<IntrusiveObject*> ptrs;
    ptrs.reserve(n);
    for (int i = 0; i < n; ++i) {
      auto obj = std::make_unique<IntrusiveObject>(i);
      ptrs.push_back(obj.get());
      list.pushBack(std::move(obj));
    }
    state.ResumeTiming();

    for (IntrusiveObject* p : ptrs) {
      benchmark::DoNotOptimize(p->removeFromList(list));
    }
  }
}
BENCHMARK(BM_IntrusiveListRemoveAll)->Arg(64)->Arg(512);

// Benchmark iterating over all items in an IntrusiveList<T> and summing values.
// Uses the next() accessor added to IntrusiveListNode<T>.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_IntrusiveListIterate(benchmark::State& state) {
  const int n = state.range(0);
  IntrusiveList<IntrusiveObject> list;
  for (int i = 0; i < n; ++i) {
    list.pushBack(std::make_unique<IntrusiveObject>(i));
  }
  for (auto _ : state) {
    int64_t sum = 0;
    for (IntrusiveObject* p = list.front(); p != nullptr; p = p->next()) {
      sum += p->value;
    }
    benchmark::DoNotOptimize(sum);
  }
}
BENCHMARK(BM_IntrusiveListIterate)->Arg(64)->Arg(512);

} // namespace Envoy
