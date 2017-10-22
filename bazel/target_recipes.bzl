# These should reflect //ci/prebuilt/BUILD declared targets. This a map from
# target in //ci/prebuilt/BUILD to the underlying build recipe in
# ci/build_container/build_recipes.
TARGET_RECIPES = {
    "ares": "cares",
    "backward": "backward",
    "event": "libevent",
    "event_pthreads": "libevent",
    # TODO(htuch): This shouldn't be a build recipe, it's a tooling dependency
    # that is external to Bazel.
    "gcovr": "gcovr",
    "googletest": "googletest",
    "tcmalloc_and_profiler": "gperftools",
    "http_parser": "http-parser",
    "luajit": "luajit",
    "nghttp2": "nghttp2",
    "rapidjson": "rapidjson",
    "ssl": "boringssl",
    "tclap": "tclap",
    "xxhash": "xxhash",
    "yaml_cpp": "yaml-cpp",
    "zlib": "zlib",
}
