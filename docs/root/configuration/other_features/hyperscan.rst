.. _config_hyperscan:

Hyperscan
=========

* :ref:`Matcher v3 API reference <envoy_v3_api_msg_extensions.matching.input_matchers.hyperscan.v3alpha.Hyperscan>`
* :ref:`Regex engine v3 API reference <envoy_v3_api_msg_extensions.regex_engines.hyperscan.v3alpha.Hyperscan>`

`Hyperscan <https://github.com/intel/hyperscan>`_ is a high-performance multiple regex matching library, which uses
hybrid automata techniques to allow simultaneous matching of large numbers of regular expressions and for the matching
of regular expressions across streams of data. Hyperscan supports the `pattern syntax
<https://intel.github.io/hyperscan/dev-reference/compilation.html#pattern-support>`_ used by PCRE.

Hyperscan is only valid in the :ref:`contrib image <install_contrib>`.

Hyperscan can be used as a matcher of :ref:`generic matching <arch_overview_generic_matching>`, or enabled as a regex
engine globally.

As a matcher of generic matching
--------------------------------

Generic matching has been implemented in a few of components and extensions in Envoy, including
:ref:`filter chain matcher <envoy_v3_api_field_config.listener.v3.Listener.filter_chain_matcher>`, :ref:`route matcher
<envoy_v3_api_field_config.route.v3.VirtualHost.matcher>` and :ref:`RBAC matcher
<envoy_v3_api_field_extensions.filters.http.rbac.v3.RBAC.matcher>`. Hyperscan matcher can be used in generic matcher as
a custom matcher in the following structure:

.. literalinclude:: _include/hyperscan_matcher.yaml
    :language: yaml
    :linenos:
    :lines: 30-35
    :caption: :download:`hyperscan_matcher.yaml <_include/hyperscan_matcher.yaml>`

The behavior of regex matching in Hyperscan matchers can be configured, please refer to the :ref:`API reference
<envoy_v3_api_msg_extensions.matching.input_matchers.hyperscan.v3alpha.Hyperscan.Regex>`.

Hyperscan matcher also supports multiple pattern matching which allows matches to be reported for several patterns
simultaneously. Multiple pattern matching can be turned on in the following structure:

.. literalinclude:: _include/hyperscan_matcher_multiple.yaml
    :language: yaml
    :linenos:
    :lines: 30-45
    :emphasize-lines: 8-16
    :caption: :download:`hyperscan_matcher_multiple.yaml <_include/hyperscan_matcher_multiple.yaml>`

As a regex engine
-----------------

Hyperscan regex engine acts in the similar behavior with the default regex engine Google RE2 like it turns on UTF-8
support by default. Hyperscan regex engine can be easily configured with the following configuration.

.. literalinclude:: _include/hyperscan_regex_engine.yaml
    :language: yaml
    :linenos:
    :lines: 45-48
    :caption: :download:`hyperscan_regex_engine.yaml <_include/hyperscan_regex_engine.yaml>`
