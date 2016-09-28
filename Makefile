DOCS_OUTPUT_DIR ?= generated/docs
DOCS_PUBLISH_DIR ?= ../envoy-docs

.PHONY: docs
docs:
	rm -fr generated/docs
	mkdir -p generated/docs
	docs/build.sh $(DOCS_OUTPUT_DIR)

.PHONY: publish_docs
publish_docs: docs
	docs/publish.sh $(DOCS_OUTPUT_DIR) $(DOCS_PUBLISH_DIR)
  
