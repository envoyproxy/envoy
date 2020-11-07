.. _faq_filter_contract:

Is there a contract my HTTP filter must adhere to?
--------------------------------------------------

* Headers encoding/decoding

  * During encoding/decoding of headers if a filter returns ``FilterHeadersStatus::StopIteration``,
    the processing can be resumed if ``encodeData()``/``decodeData()`` return
    ``FilterDataStatus::Continue`` or by explicitly calling
    ``continueEncoding()``/``continueDecoding()``.

  * During encoding/decoding of headers if a filter returns
    ``FilterHeadersStatus::StopAllIterationAndBuffer`` or
    ``FilterHeadersStatus::StopAllIterationAndWatermark``, the processing can be resumed by calling
    ``continueEncoding()``/``continueDecoding()``.

  * A filter's ``decodeHeaders()`` implementation must not return
    ``FilterHeadersStatus::ContinueAndDontEndStream`` when called with ``end_stream`` set to *false*. In this case
    ``FilterHeadersStatus::Continue`` should be returned.

  * A filter's ``encode100ContinueHeaders()`` must return ``FilterHeadersStatus::Continue`` or
    ``FilterHeadersStatus::StopIteration``.

* Data encoding/decoding

  * During encoding/decoding of data if a filter returns
    ``FilterDataStatus::StopIterationAndBuffer``, ``FilterDataStatus::StopIterationAndWatermark``,
    or ``FilterDataStatus::StopIterationNoBuffer``, the processing can be resumed if
    ``encodeData()``/``decodeData()`` return ``FilterDataStatus::Continue`` or by explicitly
    calling ``continueEncoding()``/``continueDecoding()``.

* Trailers encoding/decoding

  * During encoding/decoding of trailers if a filter returns ``FilterTrailersStatus::StopIteration``,
    the processing can be resumed by explicitly calling ``continueEncoding()``/``continueDecoding()``.

Are there well-known headers that will appear in the given headers map of ``decodeHeaders()``?
----------------------------------------------------------------------------------------------

The first filter of the decoding filter chain will have the following headers in the map:

* ``Host``
* ``Path`` (this might be omitted for CONNECT requests).

Although these headers may be omitted by one of the filters on the decoding filter chain,
they should be reinserted before the terminal filter is triggered.

