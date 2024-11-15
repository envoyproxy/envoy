.. _why_is_envoy_sending_internal_responses:

Why is Envoy sending internal responses?
========================================

One of the easiest ways to get an understanding of why Envoy sends a given local response, is to turn on trace logging. If you can run your instance with “-l trace” you will slow Envoy down significantly, but get detailed information on various events in the lifetime of each stream and connection. Any time Envoy sends an internally generated response it will log to the _debug_ level “Sending local reply with details [unique reason]” which gives you information about why the local response was sent. Each individual response detail is used at one point in the code base, be it a codec validation check or a failed route match.

If turning on debug logging is not plausible, the response details can be added to the access logs using _%RESPONSE_CODE_DETAILS%_, and again it will let you pinpoint the exact reason a given response was generated. Documentation on response code details can be found :ref:`here<config_http_conn_man_details>`

