Fixed a bug in the MaxMind geolocation provider where a database path that could not be opened,
for example a path that does not exist, crashed Envoy during configuration load. Such a
configuration is now rejected instead.
