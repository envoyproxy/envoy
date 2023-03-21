#include "library/common/jni/types/JavaVirtualMachine.h"

#include "library/common/jni/jni_support.h"

namespace Envoy {
namespace JNI {

jint JavaVirtualMachine::getJNIVersion() { return JNI_VERSION_1_6; }

JavaVM* JavaVirtualMachine::jvm_ = nullptr;

} // namespace JNI
} // namespace Envoy
