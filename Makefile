DOCS_OUTPUT_DIR ?= generated/docs

.PHONY: docs
docs:
	rm -fr generated/docs
	mkdir -p generated/docs
	docs/build.sh $(DOCS_OUTPUT_DIR)
