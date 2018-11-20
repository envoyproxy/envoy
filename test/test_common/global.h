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
 * from type-name to weak_ptr<GlobalHelper::Singleton>. That map accumulates
 * over a process lifetime and never shrinks. However, the weak_ptr will
 * generally be cleared after each test, as all shared_ptr references held in
 * Global<Type> instances are destroyed. This way, each unit-test gets a fresh
 * start.
 */
class Globals {
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
   * Manages Singleton objects that are cleaned up after all references are
   * dropped. This class must not be templatized because as a map value where
   * every singleton in the map represents a different type. Instead we
   * templatize the ptr() and ref() methods.
   */
  struct Singleton {
    virtual ~Singleton() = default;
    virtual void* ptrHelper() PURE;
    template <class Type> Type* ptr() { return static_cast<Type*>(ptrHelper()); }
    template <class Type> Type& ref() { return *ptr<Type>(); }
  };
  using SingletonSharedPtr = std::shared_ptr<Singleton>;

  /**
   * @return Type a singleton instance of Type. T must be default-constructible.
   */
  template <class Type> static SingletonSharedPtr get() { return instance().getHelper<Type>(); }

  ~Globals() = delete; // GlobalHeler is constructed once and never destroyed.

private:
  /**
   * Templatized derived class of Singleton which holds the Type object and is
   * responsible for deleting it using the correct destructor.
   */
  template <class Type> struct TypedSingleton : public Singleton {
    ~TypedSingleton() override = default;
    void* ptrHelper() override { return ptr_.get(); }

  private:
    std::unique_ptr<Type> ptr_{std::make_unique<Type>()};
  };

  Globals() = default; // Construct via Globals::instance().

  /**
   * @return Globals& a singleton for Globals.
   */
  static Globals& instance();

  template <class Type> SingletonSharedPtr getHelper() {
    Thread::LockGuard map_lock(map_mutex_);
    std::weak_ptr<Singleton>& weak_singleton_ref = singleton_map_[typeid(Type).name()];
    SingletonSharedPtr singleton = weak_singleton_ref.lock();

    if (singleton == nullptr) {
      singleton = std::make_shared<TypedSingleton<Type>>();
      weak_singleton_ref = singleton;
    }
    return singleton;
  }

  std::string describeActiveSingletonsHelper();

  Thread::MutexBasicLockable map_mutex_;
  absl::flat_hash_map<std::string, std::weak_ptr<Singleton>> singleton_map_ GUARDED_BY(map_mutex_);
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
  Type& get() { return singleton_->ref<Type>(); }
  Type* operator->() { return singleton_->ptr<Type>(); }
  Type& operator*() { return singleton_->ref<Type>(); }

private:
  Globals::SingletonSharedPtr singleton_;
};

} // namespace Test
} // namespace Envoy
