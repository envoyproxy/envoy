#include "library/common/jni/types/java_virtual_machine.h"

#include "source/common/common/assert.h"

#include "library/common/jni/jni_support.h"

namespace Envoy {
namespace JNI {

void JavaVirtualMachine::initialize(JavaVM* jvm) {
  ASSERT(jvm_ == nullptr, "JavaVM has already been set");
  ASSERT(jvm != nullptr, "Passed JavaVM is invalid");
  jvm_ = jvm;
}

JavaVM* JavaVirtualMachine::getJavaVM() { return jvm_; }

jint JavaVirtualMachine::getJNIVersion() { return JNI_VERSION_1_6; }

void JavaVirtualMachine::detachCurrentThread() {
  const auto result = jvm_->DetachCurrentThread();
  ASSERT(result == JNI_OK, "Failed to detach current thread");
}

JavaVM* JavaVirtualMachine::jvm_ = nullptr;

} // namespace JNI
} // namespace Envoy
