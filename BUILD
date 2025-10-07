load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_py_namespace()

exports_files([
    "VERSION.txt",
    "API_VERSION.txt",
    ".clang-format",
    "pytest.ini",
    ".coveragerc",
    "CODEOWNERS",
    "OWNERS.md",
    ".github/config.yml",
    "reviewers.yaml",
])

alias(
    name = "envoy",
    actual = "//source/exe:envoy",
    visibility = ["//visibility:public"],
)

alias(
    name = "envoy.stripped",
    actual = "//source/exe:envoy-static.stripped",
    visibility = ["//visibility:public"],
)

filegroup(
    name = "clang_tidy_config",
    srcs = [".clang-tidy"],
    visibility = ["//visibility:public"],
)

# These two definitions exist to help reduce Envoy upstream core code depending on extensions.
# To avoid visibility problems, see notes in source/extensions/extensions_build_config.bzl
#
# TODO(#9953) //test/config_test:__pkg__ should probably be split up and removed.
# TODO(#9953) the config fuzz tests should be moved somewhere local and //test/config_test and //test/server removed.
package_group(
    name = "extension_config",
    packages = [
        "//source/exe",
        "//source/extensions/...",
        "//test/config_test",
        "//test/extensions/...",
        "//test/server",
        "//test/server/config_validation",
        "//test/tools/...",
        "//tools/extensions/...",
    ],
)

package_group(
    name = "extension_library",
    packages = [
        "//source/extensions/...",
        "//test/extensions/...",
    ],
)

package_group(
    name = "contrib_library",
    packages = [
        "//contrib/...",
    ],
)

package_group(
    name = "mobile_library",
    packages = [
        "//mobile/...",
    ],
)

exports_files([
    "rustfmt.toml",
])

genrule(
    name = "check_cpus",
    outs = ["cpu_info.txt"],
    cmd = """
    set -e
    echo "=== CPU INFO INSIDE CONTAINER ===" > $@
    echo "Host-visible CPU count (nproc): $$(nproc)" >> $@
    echo "Python os.cpu_count(): $$(python3 -c 'import os; print(os.cpu_count())')" >> $@
    echo "" >> $@

    # Detect cgroup v1 and v2
    if [ -f /sys/fs/cgroup/cpu/cpu.cfs_quota_us ]; then
        echo "Detected cgroup v1" >> $@
        quota=$$(cat /sys/fs/cgroup/cpu/cpu.cfs_quota_us)
        period=$$(cat /sys/fs/cgroup/cpu/cpu.cfs_period_us)
        echo "cpu.cfs_quota_us = $$quota" >> $@
        echo "cpu.cfs_period_us = $$period" >> $@
        if [ "$$quota" -gt 0 ]; then
            effective=$$((quota / period))
            echo "Effective container CPU count (integer) = $$effective" >> $@
        else
            echo "No CPU limit set (quota = -1)" >> $@
        fi
    elif [ -f /sys/fs/cgroup/cpu.max ]; then
        echo "Detected cgroup v2" >> $@
        read quota period < /sys/fs/cgroup/cpu.max
        echo "cpu.max = $$quota $$period" >> $@
        if [ "$$quota" != "max" ]; then
            effective=$$((quota / period))
            echo "Effective container CPU count (integer) = $$effective" >> $@
        else
            echo "No CPU limit set (quota = max)" >> $@
        fi
    else
        echo "Could not detect cgroup info" >> $@
    fi

    echo "" >> $@
    echo "=== /proc/cpuinfo (summary) ===" >> $@
    grep -c ^processor /proc/cpuinfo >> $@
    """,
)
