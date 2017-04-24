package(default_visibility = ["//visibility:public"])

sh_test(
    name = "envoy_static_test",
    srcs = ["envoy_static_test.sh"],
    data = ["//source/exe:envoy-static"],
    # NOTE: In some environments, ASAN causes dynamic linking no matter what, so don't run this
    # test when doing ASAN.
    tags = ["no_asan"],
)
