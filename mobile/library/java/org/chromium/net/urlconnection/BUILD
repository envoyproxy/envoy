load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@rules_jvm_external//:defs.bzl", "artifact")

licenses(["notice"])  # Apache 2

# Android libraries for urlconnection

# A URL connection interface backed by Envoy-Mobile.

android_library(
    name = "urlconnection",
    srcs = [
        "CronetBufferedOutputStream.java",
        "CronetChunkedOutputStream.java",
        "CronetFixedModeOutputStream.java",
        "CronetHttpURLConnection.java",
        "CronetHttpURLStreamHandler.java",
        "CronetInputStream.java",
        "CronetOutputStream.java",
        "CronetURLStreamHandlerFactory.java",
        "MessageLoop.java",
    ],
    manifest = "URLConnectionManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/org/chromium/net",
        artifact("androidx.annotation:annotation"),
    ],
)
