#include "library/common/jni/types/JavaVirtualMachine.h"

#include "library/common/jni/jni_support.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace JNI {

void JavaVirtualMachine::initialize(JavaVM* jvm) { 
    ASSERT(jvm_ == nullptr);
    ASSERT(jvm != nullptr);
    jvm_ = jvm;
}

JavaVM* JavaVirtualMachine::getJavaVM() { return jvm_; }

jint JavaVirtualMachine::getJNIVersion() { return JNI_VERSION_1_6; }

void JavaVirtualMachine::detachCurrentThread() { jvm_->DetachCurrentThread(); }

JavaVM* JavaVirtualMachine::jvm_ = nullptr;

} // namespace JNI
} // namespace Envoy
