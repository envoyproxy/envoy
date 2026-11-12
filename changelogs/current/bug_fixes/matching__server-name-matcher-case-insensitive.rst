Fixed :ref:`ServerNameMatcher <envoy_v3_api_msg_.xds.type.matcher.v3.ServerNameMatcher>` to convert
both the configured domains and the incoming domain to lowercase before comparing them, since domain
names are not case-sensitive.
