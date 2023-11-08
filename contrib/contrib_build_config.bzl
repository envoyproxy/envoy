# See bazel/README.md for details on how this system works.
CONTRIB_EXTENSIONS = {
    #
    # Compression
    #

    "envoy.compression.qatzip.compressor":                      "//contrib/qat/compression/qatzip/compressor/source:config",

    #
    # HTTP filters
    #
    "envoy.filters.http.checksum":                              "//contrib/checksum/filters/http/source:config",
    "envoy.filters.http.dynamo":                                "//contrib/dynamo/filters/http/source:config",
    "envoy.filters.http.golang":                                "//contrib/golang/filters/http/source:config",
    "envoy.filters.http.language":                              "//contrib/language/filters/http/source:config_lib",
    "envoy.filters.http.squash":                                "//contrib/squash/filters/http/source:config",
    "envoy.filters.http.sxg":                                   "//contrib/sxg/filters/http/source:config",

    #
    # Network filters
    #

    "envoy.filters.network.client_ssl_auth":                    "//contrib/client_ssl_auth/filters/network/source:config",
    "envoy.filters.network.kafka_broker":                       "//contrib/kafka/filters/network/source/broker:config_lib",
    "envoy.filters.network.kafka_mesh":                         "//contrib/kafka/filters/network/source/mesh:config_lib",
    "envoy.filters.network.mysql_proxy":                        "//contrib/mysql_proxy/filters/network/source:config",
    "envoy.filters.network.postgres_proxy":                     "//contrib/postgres_proxy/filters/network/source:config",
    "envoy.filters.network.rocketmq_proxy":                     "//contrib/rocketmq_proxy/filters/network/source:config",
    "envoy.filters.network.generic_proxy":                      "//contrib/generic_proxy/filters/network/source:config",
    "envoy.filters.network.golang":                             "//contrib/golang/filters/network/source:config",

    #
    # Sip proxy
    #

    "envoy.filters.network.sip_proxy":                          "//contrib/sip_proxy/filters/network/source:config",
    "envoy.filters.sip.router":                                 "//contrib/sip_proxy/filters/network/source/router:config",

    #
    # Private key providers
    #

    "envoy.tls.key_providers.cryptomb":                         "//contrib/cryptomb/private_key_providers/source:config",
    "envoy.tls.key_providers.qat":                              "//contrib/qat/private_key_providers/source:config",

    #
    # Socket interface extensions
    #

    "envoy.bootstrap.vcl":                                      "//contrib/vcl/source:config",

    #
    # Input matchers
    #

    "envoy.matching.input_matchers.hyperscan":                  "//contrib/hyperscan/matching/input_matchers/source:config",

    #
    # Connection Balance extensions
    #

    "envoy.network.connection_balance.dlb":                     "//contrib/dlb/source:connection_balancer",

    #
    # Regex engines
    #

    "envoy.regex_engines.hyperscan":                            "//contrib/hyperscan/regex_engines/source:config",

    #
    # Extensions for generic proxy
    #
    "envoy.filters.generic.router":                             "//contrib/generic_proxy/filters/network/source/router:config",
    "envoy.generic_proxy.codecs.dubbo":                         "//contrib/generic_proxy/filters/network/source/codecs/dubbo:config",

    #
    # xDS delegates
    #

    "envoy.xds_delegates.kv_store":                            "//contrib/config/source:kv_store_xds_delegate",

    #
    # cluster specifier plugin
    #

    "envoy.router.cluster_specifier_plugin.golang":             "//contrib/golang/router/cluster_specifier/source:config",
}
