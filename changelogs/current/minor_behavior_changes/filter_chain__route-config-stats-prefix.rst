The filters of a filter chain that is configured per route in the filter chain filter now get the
stats prefix of the enclosing context instead of a hard-coded ``filter_chain.`` prefix. As route
specific configurations have no stats prefix today, their stats move from ``filter_chain.<name>.*``
to ``<name>.*``.
