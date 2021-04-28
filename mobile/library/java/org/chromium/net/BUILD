load("@rules_jvm_external//:defs.bzl", "artifact")

licenses(["notice"])  # Apache 2

# Cronet API fork

# Copy of the original Cronet API
android_library(
    name = "net",
    srcs = glob(["*.java"]),
    manifest = "ChromiumNetManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        artifact("androidx.annotation:annotation"),
    ],
)
