# These should reflect //ci/prebuilt/BUILD declared targets. This a map from
# target in //ci/prebuilt/BUILD to the underlying build recipe in
# ci/build_container/build_recipes.
TARGET_RECIPES = {
    "ares": "cares",
    "benchmark": "benchmark",
    "event": "libevent",
    "tcmalloc_and_profiler": "gperftools",
    "luajit": "luajit",
    "nghttp2": "nghttp2",
    "yaml_cpp": "yaml-cpp",
    "zlib": "zlib",
}
