#include "library/common/jni/types/env.h"

#include "source/common/common/assert.h"

#include "library/common/jni/jni_support.h"
#include "library/common/jni/types/java_virtual_machine.h"

namespace Envoy {
namespace JNI {

JNIEnv* Env::get() {
  if (local_env_) {
    return local_env_;
  }

  jint result = JavaVirtualMachine::getJavaVM()->GetEnv(reinterpret_cast<void**>(&local_env_),
                                                        JavaVirtualMachine::getJNIVersion());
  if (result == JNI_EDETACHED) {
    // Note: the only thread that should need to be attached is Envoy's engine std::thread.
    static const char* thread_name = "EnvoyMain";
    JavaVMAttachArgs args = {JavaVirtualMachine::getJNIVersion(), const_cast<char*>(thread_name),
                             nullptr};
    result = attach_jvm(JavaVirtualMachine::getJavaVM(), &local_env_, &args);
  }
  RELEASE_ASSERT(result == JNI_OK, "Unable to get a JVM env for the current thread");
  return local_env_;
}

thread_local JNIEnv* Env::local_env_ = nullptr;

} // namespace JNI
} // namespace Envoy
