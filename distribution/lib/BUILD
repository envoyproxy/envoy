# Copyright 2019 Tetrate
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@rules_pkg//:pkg.bzl", "pkg_tar")

package(default_visibility = ["//visibility:public"])

pkg_tar(
    name = "libcxx",
    srcs = select({
        ":bundle_libcpp": glob(["libc++*.so.1"]),
        "//conditions:default": [],
    }),
    mode = "0755",
    strip_prefix = "/packages",
)

config_setting(
    name = "bundle_libcpp",
    values = {"define": "bundle_libcpp=enabled"},
)
