Fixed the HTTP cache filters so that a response ``s-maxage`` directive implies
``proxy-revalidate`` (`RFC 9111 <https://httpwg.org/specs/rfc9111.html#rfc.section.5.2.2.10>`_)
and is not served stale when the request allows ``max-stale``.
