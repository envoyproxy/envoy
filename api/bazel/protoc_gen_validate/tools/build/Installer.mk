# Licensed under the Apache License, Version 2.0 (the "License")

# Catch all rules for Go-based tools.
$(go_tools_dir)/bin/%:
	@GOBIN=$(go_tools_dir)/bin $(go) install $($(notdir $@)@v)

# Install protoc from github.com/protocolbuffers/protobuf.
protoc-os      := $(if $(findstring $(goos),darwin),osx,$(goos))
protoc-arch    := $(if $(findstring $(goarch),arm64),aarch_64,x86_64)
protoc-version  = $(subst github.com/protocolbuffers/protobuf@v,$(empty),$($(notdir $1)@v))
protoc-archive  = protoc-$(call protoc-version,$1)-$(protoc-os)-$(protoc-arch).zip
protoc-url      = https://$(subst @,/releases/download/,$($(notdir $1)@v))/$(call protoc-archive,$1)
protoc-zip      = $(prepackaged_tools_dir)/$(call protoc-archive,$1)
$(protoc):
	@mkdir -p $(prepackaged_tools_dir)
ifeq ($(goos),linux)
	@curl -sSL $(call protoc-url,$@) -o $(call protoc-zip,$@)
	@unzip -oqq $(call protoc-zip,$@) -d $(prepackaged_tools_dir)
	@rm -f $(call protoc-zip,$@)
else
	@curl -sSL $(call protoc-url,$@) | tar xf - -C $(prepackaged_tools_dir)
	@chmod +x $@
endif
