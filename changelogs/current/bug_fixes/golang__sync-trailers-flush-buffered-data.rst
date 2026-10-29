Fixed the Go HTTP filter dropping buffered body data when an asynchronous ``DecodeData`` or
``EncodeData`` returned ``StopAndBuffer`` after the trailers had arrived and the trailers were then
handled synchronously. The trailers were forwarded without the buffered body.
