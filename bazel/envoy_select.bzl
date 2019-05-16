# Selects the given values if hot restart is enabled in the current build.
def envoy_select_hot_restart(xs, repository = ""):
    return select({
        repository + "//bazel:disable_hot_restart_or_apple": [],
        "//conditions:default": xs,
    })

# Selects the given values if Google gRPC is enabled in the current build.
def envoy_select_google_grpc(xs, repository = ""):
    return select({
        repository + "//bazel:disable_google_grpc": [],
        "//conditions:default": xs,
    })
