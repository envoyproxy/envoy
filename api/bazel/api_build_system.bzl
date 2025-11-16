load(
    ":cc_test.bzl",
    _api_cc_test = "api_cc_test",
)
load(
    ":go_test.bzl",
    _api_go_test = "api_go_test",
)
load(
    ":proto_library.bzl",
    _api_cc_py_proto_library = "api_cc_py_proto_library",
)
load(
    ":proto_package.bzl",
    _api_proto_package = "api_proto_package",
)

# This file is deprecated and maintained for backward compatibility only.
# Please use the individual rule files directly instead.
load(
    ":providers.bzl",
    _EnvoyProtoDepsInfo = "EnvoyProtoDepsInfo",
)

# Re-export for backward compatibility
EnvoyProtoDepsInfo = _EnvoyProtoDepsInfo
api_cc_py_proto_library = _api_cc_py_proto_library
api_cc_test = _api_cc_test
api_go_test = _api_go_test
api_proto_package = _api_proto_package
