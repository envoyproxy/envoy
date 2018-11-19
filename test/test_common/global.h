#pragma once

#include "common/common/lock_guard.h"
#include "common/common/thread.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Test {

/**
 * Helper class for managing Global<Type>s.
 *
 * This class is instantiated as a process-scoped singleton. It manages a map
 * from type-name to GlobalHelper::Singleton. That map accumulates over a process
 * lifetime and never shrinks. However, the Singleton objects themselves hold
 * a reference-counted class-instance pointer that is deleted and nulled after
 * all references drop, so that each unit-test gets a fresh start.
 */
class Globals {
  using MakeObjectFn = std::function<void*()>;
  using DeleteObjectFn = std::function<void(void*)>;

public:
  /**
   * Walks through all global singletons and ensures that none of them are
   * active. No singletons should be allocaed at the end of unit tests, so
   * this is called at the end of Envoy::TestRunner::RunTests().
   *
   * @return std::string empty string if quiescent, otherwise newline-separated
   *    error messages.
   */
  static std::string describeActiveSingletons() {
    return instance().describeActiveSingletonsHelper();
  }

  /**
   * Manages Singleton objects that are cleaned up after all references are dropped.
   */
  struct Singleton {
    Singleton(void* ptr) : ref_count_(1), ptr_(ptr) {}

    template <class Type> Type* ptr() { return static_cast<Type*>(ptr_); }
    template <class Type> Type& ref() { return *ptr<Type>(); }
    template <class Type> void release() {
      releaseHelper([](void* p) { delete static_cast<Type*>(p); });
    }
    void releaseHelper(DeleteObjectFn delete_object);

    Thread::MutexBasicLockable mutex_;
    uint64_t ref_count_ GUARDED_BY(mutex_);
    void* ptr_; // Chanting ptr_ is done under mutex_, but accessing it is not.
  };

  /**
   * @return Type a singleton instance of Type. T must be default-constructible.
   */
  template <class Type> static Singleton& get() {
    MakeObjectFn make_object = []() -> void* { return new Type; };

    // The real work here is done by a non-inlined function that carefully
    // manages two levels of mutexes: one for singleton_map_ and one for each
    // type of singleton. That function works with void* so that it doesn't need
    // to be templatized; the casting is done here in the templatized wrapper.
    return instance().get(typeid(Type).name(), make_object);
  }

private:
  Globals() {}         // Construct via Globals::helper().
  ~Globals() = delete; // GlobalHeler is constructed once and never destryed.

  /**
   * @return Globals& a singleton for Globals.
   */
  static Globals& instance();

  std::string describeActiveSingletonsHelper();

  Singleton& get(const std::string& type_name, MakeObjectFn make_object);

  Thread::MutexBasicLockable map_mutex_;
  absl::flat_hash_map<std::string, std::unique_ptr<Singleton>>
      singleton_map_ GUARDED_BY(map_mutex_);
};

/**
 * Helps manage classes that need to be instantiated once per server. In
 * production they must be be plumbed through call/class hierarchy, but
 * in test-code the zero-arg-constructor Mock pattern makes this impractical.
 * Instead we use self-cleaning singletons.
 *
 * Say for example you need a FooImpl plumbed through the system. In production
 * code you must propagate a FooImpl through constructors to provide access
 * where needed. For tests, everywhere a common FooImpl is required,
 * instantiate:
 *
 *   Global<FooImpl> foo;
 *
 * You can ghen access the singleton FooImpl via foo.get(). The underlying
 * FooImpl is ref-counted, and when the last TestGlobal is freed, the singleton
 * FooImpl will be destructed and the singleton pointer nulled.
 *
 * The templated type must have a zero-arg constructor. Templatizing this on an
 * int will compile, but will be hard to use as the memory will be uninitialized
 * and you will not know when instantiating it whether it needs to be
 * initialized.
 */
template <class Type> class Global {
public:
  Global() : singleton_(Globals::get<Type>()) {}
  ~Global() { singleton_.release<Type>(); }
  Type& get() { return singleton_.ref<Type>(); }
  Type* operator->() { return singleton_.ptr<Type>(); }
  Type& operator*() { return singleton_.ref<Type>(); }

private:
  Globals::Singleton& singleton_;
};

} // namespace Test
} // namespace Envoy
