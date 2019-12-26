.. _config_http_filters_header_to_metadata:

Envoy Header-To-Metadata Filter
===============================
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.header_to_metadata.v2.Config>`
* This filter should be configured with the name *envoy.filters.http.header_to_metadata*.

This filter is configured with rules that will be matched against requests and responses.
Each rule has a header and can be triggered either when the header is present or missing. When
a rule is triggered, dynamic metadata will be added based on the configuration of the rule.
The metadata can then be used for load balancing decisions, consumed from logs, etc.

A typical use case for this filter is to dynamically match requests with load balancer
subsets. For this, a given header's value would be extracted and attached to the request
as dynamic metadata which would then be used to match a subset of endpoints.

Example
-------

A sample filter configuration to route traffic to endpoints based on the presence or
absence of a version header could be:

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.header_to_metadata
      config:
        request_rules:
	  - header: x-version
	    on_header_present:
	      metadata_namespace: envoy.lb
	      key: version
	      type: STRING
	    on_header_missing:
	      metadata_namespace: envoy.lb
	      key: default
	      value: 'true'
	      type: STRING
	    remove: false


A corresponding upstream cluster configuration could be:

.. code-block:: yaml

  clusters:
    - name: versioned-cluster
      type: EDS
      lb_policy: ROUND_ROBIN
      lb_subset_config:
        fallback_policy: ANY_ENDPOINT
	subset_selectors:
	  - keys:
	      - default
          - keys:
	      - version

This would then allow requests with the `x-version` header set to be matched against
endpoints with the corresponding version. Whereas requests with that header missing
would be matched with the default endpoints.

Statistics
----------

Currently, this filter generates no statistics.
