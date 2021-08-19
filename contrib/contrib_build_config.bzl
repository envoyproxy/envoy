# See bazel/README.md for details on how this system works.
CONTRIB_EXTENSIONS = {
    #
    # HTTP filters
    #

    "envoy.filters.http.squash":                        "//contrib/squash/filters/http/source:config",

    #
    # Network filters
    #

    "envoy.filters.network.kafka_broker":                         "//contrib/kafka/filters/network/source:kafka_broker_config_lib",
}
