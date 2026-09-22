Fixed a bug in the MaxMind geolocation provider where a configuration that had previously been used
and then torn down could return a null driver instead of building a new provider.
