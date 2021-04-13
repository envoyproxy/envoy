# Modified from https://github.com/bazelbuild/bazel/blob/1.1.0/distdir.bzl

# Copyright 2018 The Bazel Authors. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Defines a repository rule that generates an archive consisting of the specified files to fetch"""

_BUILD = """
load("@bazel_tools//tools/build_defs/pkg:pkg.bzl", "pkg_tar")
pkg_tar(
  name="archives",
  srcs = {srcs},
  package_dir = "{dirname}",
  visibility = ["//visibility:public"],
)
"""

def _distdir_tar_impl(ctx):
    for name in ctx.attr.archives:
        ctx.download(ctx.attr.urls[name], name, ctx.attr.sha256[name], False)
    ctx.file("WORKSPACE", "")
    ctx.file(
        "BUILD",
        _BUILD.format(srcs = ctx.attr.archives, dirname = ctx.attr.dirname),
    )

_distdir_tar_attrs = {
    "archives": attr.string_list(),
    "sha256": attr.string_dict(),
    "urls": attr.string_list_dict(),
    "dirname": attr.string(default = "distdir"),
}

distdir_tar = repository_rule(
    implementation = _distdir_tar_impl,
    attrs = _distdir_tar_attrs,
)
