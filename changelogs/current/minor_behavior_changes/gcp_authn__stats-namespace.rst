The filter's statistics are now emitted in the ``http.<stat_prefix>.gcp_authn.`` namespace instead of
directly in the HTTP connection manager's ``http.<stat_prefix>.`` namespace, matching the convention
used by the other HTTP filters. For example ``http.ingress.retrieve_audience_failed`` is now
``http.ingress.gcp_authn.retrieve_audience_failed``. Dashboards and alerts referring to the old names
need to be updated.
