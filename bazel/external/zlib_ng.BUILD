# Native Bazel BUILD file for zlib-ng.
# Based on envoyproxy/toolshed bazel-registry/modules/zlib-ng BUILD file,
# which is derived from LLVM's zlib-ng.BUILD.

load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

genrule(
    # The input template is identical to the CMake output.
    name = "zconf_gen",
    srcs = ["zconf.h.in"],
    outs = ["zconf.h"],
    cmd = "cp $(SRCS) $(OUTS)",
)

genrule(
    # Generate zlib.h from zlib.h.in by removing @ZLIB_SYMBOL_PREFIX@ placeholders.
    # The toolshed version just copies, but that leaves @ZLIB_SYMBOL_PREFIX@ in the output.
    name = "zlib_gen",
    srcs = ["zlib.h.in"],
    outs = ["zlib.h"],
    cmd = "sed 's/@ZLIB_SYMBOL_PREFIX@//g' $(SRCS) > $(OUTS)",
)

genrule(
    # Use the empty name mangling header for ZLIB_COMPAT mode (no symbol prefix).
    name = "zlib_name_mangling_gen",
    srcs = ["zlib_name_mangling.h.empty"],
    outs = ["zlib_name_mangling.h"],
    cmd = "cp $(SRCS) $(OUTS)",
)

genrule(
    # Generate gzread.c from gzread.c.in by removing @ZLIB_SYMBOL_PREFIX@ placeholders.
    name = "gzread_gen",
    srcs = ["gzread.c.in"],
    outs = ["gzread.c"],
    cmd = "sed 's/@ZLIB_SYMBOL_PREFIX@//g' $(SRCS) > $(OUTS)",
)

cc_library(
    name = "zlib_ng",
    srcs = [
        "adler32.c",
        "compress.c",
        "cpu_features.c",
        "crc32.c",
        "crc32_braid_comb.c",
        "deflate.c",
        "deflate_fast.c",
        "deflate_huff.c",
        "deflate_medium.c",
        "deflate_quick.c",
        "deflate_rle.c",
        "deflate_slow.c",
        "deflate_stored.c",
        "functable.c",
        "gzlib.c",
        "gzwrite.c",
        "infback.c",
        "inflate.c",
        "inftrees.c",
        "insert_string.c",
        "insert_string_roll.c",
        "trees.c",
        "uncompr.c",
        "zutil.c",
        # Generic architecture source files.
        "arch/generic/adler32_c.c",
        "arch/generic/adler32_fold_c.c",
        "arch/generic/chunkset_c.c",
        "arch/generic/compare256_c.c",
        "arch/generic/crc32_braid_c.c",
        "arch/generic/crc32_chorba_c.c",
        "arch/generic/crc32_fold_c.c",
        "arch/generic/slide_hash_c.c",
        # Generated source file.
        ":gzread_gen",
    ],
    hdrs = [
        # Public headers.
        ":zlib_gen",
        ":zconf_gen",
        ":zlib_name_mangling_gen",
        # Internal headers.
        "adler32_p.h",
        "arch_functions.h",
        "chunkset_tpl.h",
        "compare256_rle.h",
        "cpu_features.h",
        "crc32.h",
        "crc32_braid_comb_p.h",
        "crc32_braid_p.h",
        "crc32_braid_tbl.h",
        "deflate.h",
        "deflate_p.h",
        "fallback_builtins.h",
        "functable.h",
        "gzguts.h",
        "inffast_tpl.h",
        "inffixed_tbl.h",
        "inflate.h",
        "inflate_p.h",
        "inftrees.h",
        "insert_string_tpl.h",
        "match_tpl.h",
        "trees.h",
        "trees_emit.h",
        "trees_tbl.h",
        "zbuild.h",
        "zendian.h",
        "zmemory.h",
        "zutil.h",
        "zutil_p.h",
        # Generic architecture headers.
        "arch/generic/chunk_128bit_perm_idx_lut.h",
        "arch/generic/chunk_256bit_perm_idx_lut.h",
        "arch/generic/chunk_permute_table.h",
        "arch/generic/compare256_p.h",
        "arch/generic/generic_functions.h",
    ],
    copts = select({
        "@platforms//os:windows": [
            "/wd4127",  # conditional expression is constant
            "/wd4131",  # old-style declarator
            "/wd4244",  # possible loss of data
            "/wd4245",  # signed/unsigned mismatch
            "/wd4267",  # conversion from size_t
            "/wd4996",  # deprecated functions
        ],
        "@platforms//os:macos": [
            "-std=c11",
            "-Wno-deprecated-non-prototype",
            "-Wno-unused-variable",
            "-Wno-implicit-function-declaration",
        ],
        "//conditions:default": [
            "-std=c11",
            "-Wno-deprecated-non-prototype",
            "-Wno-unused-variable",
            "-Wno-implicit-function-declaration",
        ],
    }),
    # Needed for arch/generic includes and strip_include_prefix for zlib.h.
    includes = ["."],
    local_defines = [
        "ZLIB_COMPAT",
        "WITH_GZFILEOP",
        "WITH_OPTIM",
        "WITH_NEW_STRATEGIES",
        # Enable all generic C fallbacks for the function table.
        # This ensures the functable is properly initialized on all platforms.
        "WITH_ALL_FALLBACKS",
    ] + select({
        "@platforms//os:windows": ["_CRT_NONSTDC_NO_WARNINGS"],
        "//conditions:default": [],
    }),
    strip_include_prefix = ".",
    visibility = ["//visibility:public"],
)
