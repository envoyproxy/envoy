licenses(["notice"])  # Apache 2

android_binary(
    name = "hello_envoy",
    srcs = glob([
        "MainActivity.java",
        "Response.java",
        "ResponseRecyclerViewAdapter.java",
        "ResponseViewHolder.java",
    ]),
    custom_package = "io.envoyproxy.envoymobile.helloenvoy",
    manifest = "AndroidManifest.xml",
    resource_files = glob(["res/**"]),
    deps = [
        "//dist:envoy_mobile_android",
        "@androidsdk//com.android.support:appcompat-v7-25.0.0",
        "@androidsdk//com.android.support:recyclerview-v7-25.0.0",
    ],
)
