# These should reflect //ci/prebuilt/BUILD declared targets. This a map from
# target in //ci/prebuilt/BUILD to the underlying build recipe in
# ci/build_container/build_recipes.
TARGET_RECIPES = {
    "tcmalloc_and_profiler": "gperftools",
    "tcmalloc_debug": "gperftools",
    "luajit": "luajit",
}
