Fixed a bug in the HTTP cache v2 filter where body and trailer reset callbacks could run
on the wrong worker thread when a cache operation failed or a body request was out of range.
