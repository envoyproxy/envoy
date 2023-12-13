load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@io_bazel_rules_kotlin//kotlin:android.bzl", "kt_android_local_test")
load("@io_bazel_rules_kotlin//kotlin:jvm.bzl", "kt_jvm_test")
load("//bazel:kotlin_lib.bzl", "native_lib_name")

def _internal_kt_test(name, srcs, deps = [], data = [], jvm_flags = [], repository = "", exec_properties = {}):
    # This is to work around the issue where we have specific implementation functionality which
    # we want to avoid consumers to use but we want to unit test
    dep_srcs = []
    for dep in deps:
        # We'll resolve only the targets in `//library/kotlin/io/envoyproxy/envoymobile`
        if dep.startswith(repository + "//library/kotlin/io/envoyproxy/envoymobile"):
            dep_srcs.append(dep + "_srcs")
        elif dep.startswith(repository + "//library/java/io/envoyproxy/envoymobile"):
            dep_srcs.append(dep + "_srcs")

    kt_jvm_test(
        name = name,
        test_class = "io.envoyproxy.envoymobile.bazel.EnvoyMobileTestSuite",
        srcs = srcs + dep_srcs,
        deps = [
            repository + "//bazel:envoy_mobile_test_suite",
            "@maven//:org_assertj_assertj_core",
            "@maven//:junit_junit",
            "@maven//:org_mockito_mockito_inline",
            "@maven//:org_mockito_mockito_core",
        ] + deps,
        data = data,
        jvm_flags = jvm_flags,
        exec_properties = exec_properties,
    )

# A simple macro to define the JVM flags that are common for envoy_mobile_jni_kt_test and
# envoy_mobile_android_test.
def jvm_flags(lib_name):
    return [
        "-Djava.library.path=library/common/jni:test/common/jni",
        "-Denvoy_jni_library_name={}".format(lib_name),
        "-Xcheck:jni",
    ] + select({
        "@envoy//bazel:disable_google_grpc": ["-Denvoy_jni_google_grpc_disabled=true"],
        "//conditions:default": [],
    }) + select({
        "@envoy//bazel:disable_envoy_mobile_xds": ["-Denvoy_jni_envoy_mobile_xds_disabled=true"],
        "//conditions:default": [],
    })

# A basic macro to make it easier to declare and run kotlin tests which depend on a JNI lib
# This will create the native .so binary (for linux) and a .jnilib (for macOS) look up
def envoy_mobile_jni_kt_test(name, srcs, native_lib_name, native_deps = [], deps = [], repository = "", exec_properties = {}):
    _internal_kt_test(
        name,
        srcs,
        deps,
        data = native_deps,
        jvm_flags = jvm_flags(native_lib_name),
        repository = repository,
        exec_properties = exec_properties,
    )

# A basic macro to make it easier to declare and run kotlin tests
#
# Ergonomic improvements include:
# 1. Avoiding the need to declare the test_class which requires a fully qualified class name (example below)
# 2. Avoiding the need to redeclare common unit testing dependencies like JUnit
# 3. Ability to run more than one test file per target
# 4. Ability to test internal envoy mobile entities
# Usage example:
# load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_kt_test)
#
# envoy_mobile_kt_test(
#     name = "example_kotlin_test",
#     srcs = [
#         "ExampleTest.kt",
#     ],
# )
def envoy_mobile_kt_test(name, srcs, deps = [], repository = "", exec_properties = {}):
    _internal_kt_test(name, srcs, deps, repository = repository, exec_properties = exec_properties)

# A basic macro to run android based (robolectric) tests with native dependencies
def envoy_mobile_android_test(name, srcs, native_lib_name, deps = [], native_deps = [], repository = "", exec_properties = {}):
    android_library(
        name = name + "_test_lib",
        custom_package = "io.envoyproxy.envoymobile.test",
        manifest = repository + "//bazel:test_manifest.xml",
        visibility = ["//visibility:public"],
        data = native_deps,
        exports = deps,
        testonly = True,
    )
    kt_android_local_test(
        name = name,
        srcs = srcs,
        data = native_deps,
        deps = deps + [
            repository + "//bazel:envoy_mobile_test_suite",
            "@maven//:androidx_annotation_annotation",
            "@maven//:androidx_test_core",
            "@maven//:androidx_test_ext_junit",
            "@maven//:androidx_test_runner",
            "@maven//:androidx_test_monitor",
            "@maven//:androidx_test_rules",
            "@maven//:org_robolectric_robolectric",
            "@robolectric//bazel:android-all",
            "@maven//:org_assertj_assertj_core",
            "@maven//:junit_junit",
            "@maven//:org_mockito_mockito_inline",
            "@maven//:org_mockito_mockito_core",
            "@maven//:com_squareup_okhttp3_okhttp",
            "@maven//:com_squareup_okhttp3_mockwebserver",
            "@maven//:com_squareup_okio_okio_jvm",
            "@maven//:org_hamcrest_hamcrest",
            "@maven//:com_google_truth_truth",
        ],
        manifest = repository + "//bazel:test_manifest.xml",
        custom_package = "io.envoyproxy.envoymobile.tests",
        test_class = "io.envoyproxy.envoymobile.bazel.EnvoyMobileTestSuite",
        jvm_flags = jvm_flags(native_lib_name),
        exec_properties = exec_properties,
    )
