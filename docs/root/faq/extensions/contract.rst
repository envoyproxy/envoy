.. _faq_filter_contract:

Is there a contract my HTTP filter must adhere to?
==================================================

* A filter's ``decodeData`` implementation must not return ``FilterHeadersStatus::ContinueAndEndStream`` when called with ``end_stream`` set.
  In this case the ``FilterHeadersStatus::Continue`` should be returned.
