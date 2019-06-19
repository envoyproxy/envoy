package io.envoyproxy.envoymobile;

/**
 * This class is used as a workaround to compile cc_libraries into a java_library for
 * kt_android_library for Envoy JNI in kotlin. There's an issue with transitive dependencies
 * related to kt_jvm_library which errors with a message:
 * <p>
 * ...in deps attribute of kt_jvm_library rule //:android_lib_kt:
 * '//library/common:envoy_jni_interface_lib' does not have mandatory providers: 'JavaInfo' <p>
 * Could be related to: https://github.com/bazelbuild/rules_kotlin/issues/132
 */
public class EnvoyKotlinEmptyClass {}
