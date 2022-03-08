.. _arch_overview_matching_inputs:

Matching Inputs
===============

Matching inputs define a way to extract an input value used for matching using
one of the pre-defined (exact, prefix) or a custom matching algorithm. The
input functions are context-sensitive. For example, HTTP header inputs are
applicable only in HTTP contexts, e.g. for matching HTTP requests.

.. _arch_overview_matching_http_inputs:

HTTP Input Functions
####################

These input  are available for matching HTTP requests.

* :ref:`Request header value <envoy_v3_api_msg_type.matcher.v3.HttpRequestHeaderMatchInput>`.
* :ref:`Request trailer value <envoy_v3_api_msg_type.matcher.v3.HttpRequestTrailerMatchInput>`.
* :ref:`Response header value <envoy_v3_api_msg_type.matcher.v3.HttpResponseHeaderMatchInput>`.
* :ref:`Response trailer value <envoy_v3_api_msg_type.matcher.v3.HttpResponseTrailerMatchInput>`.

.. _arch_overview_matching_network_inputs:

Network Input Functions
#######################

These input functions are available for matching TCP connections.

* :ref:`Destination IP <envoy_v3_api_msg_extensions.matching.common_inputs.network.v3.DestinationIPInput>`.
* :ref:`Destination port <envoy_v3_api_msg_extensions.matching.common_inputs.network.v3.DestinationPortInput>`.
* :ref:`Source IP <envoy_v3_api_msg_extensions.matching.common_inputs.network.v3.SourceIPInput>`.
* :ref:`Direct source IP <envoy_v3_api_msg_extensions.matching.common_inputs.network.v3.DirectSourceIPInput>`.
* :ref:`Source port <envoy_v3_api_msg_extensions.matching.common_inputs.network.v3.SourcePortInput>`.
* :ref:`Source type <envoy_v3_api_msg_extensions.matching.common_inputs.network.v3.SourceTypeInput>`.
* :ref:`Server name <envoy_v3_api_msg_extensions.matching.common_inputs.network.v3.ServerNameInput>`.
* :ref:`Transport protocol <envoy_v3_api_msg_extensions.matching.common_inputs.network.v3.TransportProtocolInput>`.
* :ref:`Application protocol <envoy_v3_api_msg_extensions.matching.common_inputs.network.v3.ApplicationProtocolInput>`.
