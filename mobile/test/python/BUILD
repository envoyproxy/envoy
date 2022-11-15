load("@mobile_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_test")

licenses(["notice"])  # Apache 2

py_test(
    name = "runtest_unit",
    srcs = glob([
        "conftest.py",
        "echo_server.py",
        "runner.py",
        "unit/**/*.py",
    ]),
    args = [
        "test/python/",
        "-s",
        "-m",
        "\"not standalone\"",
    ],
    main = "runner.py",
    python_version = "PY3",
    deps = [
        "//library/python:envoy_requests",
        requirement("pytest"),
        requirement("pytest-asyncio"),
        requirement("requests"),
    ],
)

py_test(
    name = "runtest_integration",
    srcs = glob([
        "conftest.py",
        "echo_server.py",
        "runner.py",
        "integration/**/*.py",
    ]),
    args = [
        "test/python/",
        "-s",
        "-m",
        "\"not standalone\"",
    ],
    main = "runner.py",
    python_version = "PY3",
    deps = [
        "//library/python:envoy_requests",
        requirement("pytest"),
    ],
)

# these are all separated
# so that there is one process per test
# and each can construct their own Engine
py_test(
    name = "runtest_pre_build_engine_asyncio",
    srcs = glob([
        "conftest.py",
        "echo_server.py",
        "runner.py",
        "unit/**/*.py",
    ]),
    args = [
        "test/python/unit/test_pre_build_engine.py::test_pre_build_engine_asyncio",
        "-s",
    ],
    main = "runner.py",
    python_version = "PY3",
    deps = [
        "//library/python:envoy_requests",
        requirement("pytest"),
        requirement("pytest-asyncio"),
    ],
)

py_test(
    name = "runtest_pre_build_engine_gevent",
    srcs = glob([
        "conftest.py",
        "echo_server.py",
        "runner.py",
        "unit/**/*.py",
    ]),
    args = [
        "test/python/unit/test_pre_build_engine.py::test_pre_build_engine_gevent",
        "-s",
    ],
    main = "runner.py",
    python_version = "PY3",
    deps = [
        "//library/python:envoy_requests",
        requirement("pytest"),
    ],
)

py_test(
    name = "runtest_pre_build_engine_threading",
    srcs = glob([
        "conftest.py",
        "echo_server.py",
        "runner.py",
        "unit/**/*.py",
    ]),
    args = [
        "test/python/unit/test_pre_build_engine.py::test_pre_build_engine_threading",
        "-s",
    ],
    main = "runner.py",
    python_version = "PY3",
    deps = [
        "//library/python:envoy_requests",
        requirement("pytest"),
    ],
)

# NOTE: these tests do not need to be run in CI by default
# but we want to automatically detect new python tests
# so uncomment to run locally
# py_test(
#     name = "benchmark_envoy_vs_requests",
#     srcs = [
#         "benchmark/__init__.py",
#         "benchmark/envoy_vs_requests.py",
#         "runner.py",
#     ],
#     args = [
#         "test/python/benchmark/envoy_vs_requests.py",
#         "-s",
#     ],
#     main = "runner.py",
#     python_version = "PY3",
#     deps = [
#         "//library/python:envoy_requests",
#         requirement("pytest"),
#         requirement("pytest-benchmark"),
#         requirement("requests"),
#     ],
# )
