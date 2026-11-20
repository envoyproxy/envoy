Fixed the ``server.seconds_until_first_ocsp_response_expiring`` statistic, which kept reporting the
expiration of the last OCSP response once no TLS context had an OCSP response, for example after a
certificate with an OCSP staple was replaced by one without a staple or its listener was removed. It
now drops to 0, its value before the first OCSP response.
