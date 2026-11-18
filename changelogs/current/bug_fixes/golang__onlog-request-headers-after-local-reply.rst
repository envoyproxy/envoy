Fixed the Go HTTP filter's access log callbacks not seeing the request headers when an earlier
filter sent a local reply before the Go filter decoded them. Reading the request headers in
``OnLog`` panicked with an invalid phase error, and request properties such as
``request.referer`` were not found.
