licenses(["notice"])  # Apache 2

android_binary(
    name = "hello_envoy",
    srcs = [
        "MainActivity.java",
    ],
    custom_package = "io.envoyproxy.envoymobile.helloenvoy",
    manifest = "AndroidManifest.xml",
    resource_files = [
        "res/layout/activity_main.xml",
        "res/raw/config.yaml",
    ],
    deps = [
        "//dist:envoy_mobile_android",
        "//examples/kotlin/shared:hello_envoy_shared_lib",
        "@androidsdk//com.android.support:appcompat-v7-25.0.0",
        "@androidsdk//com.android.support:recyclerview-v7-25.0.0",
    ],
)
