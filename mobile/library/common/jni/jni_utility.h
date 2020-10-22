#include <jni.h>

#include "library/common/types/c_types.h"

// NOLINT(namespace-envoy)

void set_vm(JavaVM* vm);

JavaVM* get_vm();

JNIEnv* get_env();

void jvm_detach_thread();

void jni_delete_global_ref(void* context);

void jni_delete_const_global_ref(const void* context);

int unbox_integer(JNIEnv* env, jobject boxedInteger);

envoy_data array_to_native_data(JNIEnv* env, jbyteArray j_data);

envoy_data buffer_to_native_data(JNIEnv* env, jobject j_data);

envoy_data* buffer_to_native_data_ptr(JNIEnv* env, jobject j_data);

envoy_headers to_native_headers(JNIEnv* env, jobjectArray headers);

envoy_headers* to_native_headers_ptr(JNIEnv* env, jobjectArray headers);
