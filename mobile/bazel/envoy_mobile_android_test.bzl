load("@build_bazel_rules_android//android:rules.bzl", "android_local_test")
load("@io_bazel_rules_kotlin//kotlin:android.bzl", "kt_android_local_test")
load("//bazel:envoy_mobile_android_jni.bzl", "native_lib_name")

# A simple macro to define the JVM flags.
def jvm_flags(lib_name):
    return [
        "-Djava.library.path=library/jni:test/jni",
        "-Denvoy_jni_library_name={}".format(lib_name),
        "-Xcheck:jni",
    ] + select({
        "@envoy//bazel:disable_google_grpc": ["-Denvoy_jni_google_grpc_disabled=true"],
        "//conditions:default": [],
    })

def _contains_all(srcs, extension):
    """
    Returns True if all elements in `srcs` contain the specified `extension`.
    """
    for src in srcs:
        if not src.endswith(extension):
            return False
    return True

# A basic macro to run android based (robolectric) tests with native dependencies
def envoy_mobile_android_test(name, srcs, test_class, native_lib_name = "", deps = [], native_deps = [], repository = "", exec_properties = {}, **kwargs):
    dependencies = deps + [
        "@maven//:androidx_annotation_annotation",
        "@maven//:androidx_test_core",
        "@maven//:androidx_test_ext_junit",
        "@maven//:androidx_test_runner",
        "@maven//:androidx_test_monitor",
        "@maven//:androidx_test_rules",
        "@maven//:org_robolectric_robolectric",
        "@maven//:junit_junit",
        "@maven//:org_mockito_mockito_inline",
        "@maven//:org_mockito_mockito_core",
        "@maven//:com_squareup_okhttp3_okhttp",
        "@maven//:com_squareup_okhttp3_mockwebserver",
        "@maven//:com_squareup_okio_okio_jvm",
        "@maven//:org_hamcrest_hamcrest",
        "@maven//:com_google_truth_truth",
        "@maven//:org_robolectric_shadows_framework",
        "@robolectric//bazel:android-all",
    ]
    if _contains_all(srcs, ".java"):
        android_local_test(
            name = name,
            srcs = srcs,
            data = native_deps,
            deps = dependencies,
            manifest = repository + "//bazel:test_manifest.xml",
            custom_package = test_class.rsplit(".", 1)[0],
            test_class = test_class,
            jvm_flags = jvm_flags(native_lib_name),
            exec_properties = exec_properties,
            **kwargs
        )
    elif _contains_all(srcs, ".kt"):
        kt_android_local_test(
            name = name,
            srcs = srcs,
            data = native_deps,
            deps = dependencies,
            manifest = repository + "//bazel:test_manifest.xml",
            custom_package = test_class.rsplit(".", 1)[0],
            test_class = test_class,
            jvm_flags = jvm_flags(native_lib_name),
            exec_properties = exec_properties,
            **kwargs
        )
    else:
        fail("Mixing Java and Kotlin in 'srcs' is not supported.")
