# See bazel/README.md for details on how this system works.
CONTRIB_EXTENSIONS = {
    #
    # HTTP filters
    #

    "envoy.filters.http.language":                              "//contrib/language/filters/http/source:config_lib",
    "envoy.filters.http.squash":                                "//contrib/squash/filters/http/source:config",
    "envoy.filters.http.sxg":                                   "//contrib/sxg/filters/http/source:config",

    #
    # Network filters
    #

    "envoy.filters.network.kafka_broker":                       "//contrib/kafka/filters/network/source:kafka_broker_config_lib",
    "envoy.filters.network.kafka_mesh":                         "//contrib/kafka/filters/network/source/mesh:config_lib",
    "envoy.filters.network.mysql_proxy":                        "//contrib/mysql_proxy/filters/network/source:config",
    "envoy.filters.network.postgres_proxy":                     "//contrib/postgres_proxy/filters/network/source:config",
    "envoy.filters.network.rocketmq_proxy":                     "//contrib/rocketmq_proxy/filters/network/source:config",

    #
    # Sip proxy
    #

    "envoy.filters.network.sip_proxy":                          "//contrib/sip_proxy/filters/network/source:config",
    "envoy.filters.sip.router":                                 "//contrib/sip_proxy/filters/network/source/router:config",

    #
    # Private key providers
    #

    "envoy.tls.key_providers.cryptomb":                         "//contrib/cryptomb/private_key_providers/source:config",

    #
    # Socket interface extensions
    #

    "envoy.bootstrap.vcl":                                      "//contrib/vcl/source:config",

    #
    # Input matchers
    #

    "envoy.matching.input_matchers.hyperscan":                  "//contrib/hyperscan/matching/input_matchers/source:config",
}
