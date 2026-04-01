# Licensed under the Apache License, Version 2.0 (the "License")

# Disable cgo.
export CGOENABLED := 0

# Reference: https://developers.google.com/protocol-buffers/docs/reference/go/faq#namespace-conflict.
export GOLANG_PROTOBUF_REGISTRATION_CONFLICT := warn
