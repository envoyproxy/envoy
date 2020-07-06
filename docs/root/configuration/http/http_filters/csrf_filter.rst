.. _config_http_filters_csrf:

CSRF
====

This is a filter which prevents Cross-Site Request Forgery based on a route or virtual host settings.
At it's simplest, CSRF is an attack that occurs when a malicious third-party
exploits a vulnerability that allows them to submit an undesired request on the
user's behalf.

A real-life example is cited in section 1 of `Robust Defenses for Cross-Site Request Forgery <https://seclab.stanford.edu/websec/csrf/csrf.pdf>`_:

    "For example, in late 2007 [42], Gmail had a CSRF vulnerability. When a Gmail user visited
    a malicious site, the malicious site could generate a request to Gmail that Gmail treated
    as part of its ongoing session with the victim. In November 2007, a web attacker exploited
    this CSRF vulnerability to inject an email filter into David Airey’s Gmail account [1]."

There are many ways to mitigate CSRF, some of which have been outlined in the
`OWASP Prevention Cheat Sheet <https://github.com/OWASP/CheatSheetSeries/blob/5a1044e38778b42a19c6adbb4dfef7a0fb071099/cheatsheets/Cross-Site_Request_Forgery_Prevention_Cheat_Sheet.md>`_.
This filter employs a stateless mitigation pattern known as origin verification.

This pattern relies on two pieces of information used in determining if
a request originated from the same host.
* The origin that caused the user agent to issue the request (source origin).
* The origin that the request is going to (target origin).

When the filter is evaluating a request, it ensures both pieces of information are present
and compares their values. If the source origin is missing or the origins do not match
the request is rejected. The exception to this being if the source origin has been
added to the policy as valid. Because CSRF attacks specifically target state-changing
requests, the filter only acts on HTTP requests that have a state-changing method
(POST, PUT, etc.).

  .. note::
    Due to differing functionality between browsers this filter will determine
    a request's source origin from the Origin header. If that is not present it will
    fall back to the host and port value from the requests Referer header.


For more information on CSRF please refer to the pages below.

* https://www.owasp.org/index.php/Cross-Site_Request_Forgery_%28CSRF%29
* https://seclab.stanford.edu/websec/csrf/csrf.pdf
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.csrf.v3.CsrfPolicy>`

  .. note::

    This filter should be configured with the name *envoy.filters.http.csrf*.

.. _csrf-configuration:

Configuration
-------------

The CSRF filter supports the ability to extend the source origins it will consider
valid. The reason it is able to do this while still mitigating cross-site request
forgery attempts is because the target origin has already been reached by the time
front-envoy is applying the filter. This means that while endpoints may support
cross-origin requests they are still protected from malicious third-parties who
have not been allowlisted.

It's important to note that requests should generally originate from the same
origin as the target but there are use cases where that may not be possible.
For example, if you are hosting a static site on a third-party vendor but need
to make requests for tracking purposes.

.. warning::

  Additional origins can be either an exact string, regex pattern, prefix string,
  or suffix string. It's advised to be cautious when adding regex, prefix, or suffix
  origins since an ambiguous origin can pose a security vulnerability.

.. _csrf-runtime:

Runtime
-------

The fraction of requests for which the filter is enabled can be configured via the :ref:`runtime_key
<envoy_v3_api_field_config.core.v3.RuntimeFractionalPercent.runtime_key>` value of the :ref:`filter_enabled
<envoy_v3_api_field_extensions.filters.http.csrf.v3.CsrfPolicy.filter_enabled>` field.

The fraction of requests for which the filter is enabled in shadow-only mode can be configured via
the :ref:`runtime_key <envoy_v3_api_field_config.core.v3.RuntimeFractionalPercent.runtime_key>` value of the
:ref:`shadow_enabled <envoy_v3_api_field_extensions.filters.http.csrf.v3.CsrfPolicy.shadow_enabled>` field.
When enabled in shadow-only mode, the filter will evaluate the request's *Origin* and *Destination*
to determine if it's valid but will not enforce any policies.

.. note::

  If both ``filter_enabled`` and ``shadow_enabled`` are on, the ``filter_enabled``
  flag will take precedence.

.. _csrf-statistics:

Statistics
----------

The CSRF filter outputs statistics in the <stat_prefix>.csrf.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  missing_source_origin, Counter, Number of requests that are missing a source origin header.
  request_invalid, Counter, Number of requests whose source and target origins do not match.
  request_valid, Counter, Number of requests whose source and target origins match.
