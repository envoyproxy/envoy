Added a dynamic modules HTTP/1 header formatter extension
(``envoy.http.stateful_header_formatters.dynamic_modules``) that lets a dynamic module decide the
casing of header keys written on the wire, which until now could only be done with the fixed policy
of the preserve case formatter.
