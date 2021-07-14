load("@rules_python//python:defs.bzl", "py_binary", "py_test")

licenses(["notice"])  # Apache 2

py_binary(
    name = "proto_breaking_change_detector",
    srcs = ["detector.py"],
    data = [
        "//tools/api_proto_breaking_change_detector:buf.lock",
        "//tools/api_proto_breaking_change_detector:buf.yaml",
        "@com_github_bufbuild_buf//:buf",
    ],
    main = "detector.py",
    tags = ["manual"],
    deps = [
        "//tools:run_command",
    ],
)

py_test(
    name = "proto_breaking_change_detector_test",
    srcs = ["detector_test.py"],
    data = [
        "//tools/testdata/api_proto_breaking_change_detector:allowed/test_add_comment_current",
        "//tools/testdata/api_proto_breaking_change_detector:allowed/test_add_comment_next",
        "//tools/testdata/api_proto_breaking_change_detector:allowed/test_add_enum_value_current",
        "//tools/testdata/api_proto_breaking_change_detector:allowed/test_add_enum_value_next",
        "//tools/testdata/api_proto_breaking_change_detector:allowed/test_add_field_current",
        "//tools/testdata/api_proto_breaking_change_detector:allowed/test_add_field_next",
        "//tools/testdata/api_proto_breaking_change_detector:allowed/test_add_option_current",
        "//tools/testdata/api_proto_breaking_change_detector:allowed/test_add_option_next",
        "//tools/testdata/api_proto_breaking_change_detector:allowed/test_force_breaking_change_current",
        "//tools/testdata/api_proto_breaking_change_detector:allowed/test_force_breaking_change_next",
        "//tools/testdata/api_proto_breaking_change_detector:allowed/test_remove_and_reserve_field_current",
        "//tools/testdata/api_proto_breaking_change_detector:allowed/test_remove_and_reserve_field_next",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_field_from_oneof_current",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_field_from_oneof_next",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_field_id_current",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_field_id_next",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_field_name_current",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_field_name_next",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_field_plurality_current",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_field_plurality_next",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_field_to_oneof_current",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_field_to_oneof_next",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_field_type_current",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_field_type_next",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_package_name_current",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_package_name_next",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_pgv_field_current",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_pgv_field_next",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_pgv_message_current",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_pgv_message_next",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_pgv_oneof_current",
        "//tools/testdata/api_proto_breaking_change_detector:breaking/test_change_pgv_oneof_next",
        "@com_github_bufbuild_buf//:buf",
    ],
    main = "detector_test.py",
    python_version = "PY3",
    srcs_version = "PY3",
    tags = ["manual"],
    visibility = ["//visibility:public"],
    deps = [
        ":proto_breaking_change_detector",
        "@rules_python//python/runfiles",
    ],
)
