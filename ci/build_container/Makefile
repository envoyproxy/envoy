# The individual build recipe scripts must contain sufficient information (e.g.  SHA, URL, repo,
# version number, etc.) to uniquely identify the revision of the upstream dependency.  This allows
# make to pick up changes with a simple direct dependency on the build recipe.

all: $(THIRDPARTY_DEPS)/libevent.dep \
	$(THIRDPARTY_DEPS)/boringssl.dep \
	$(THIRDPARTY_DEPS)/gperftools.dep \
	$(THIRDPARTY_DEPS)/nghttp2.dep \
	$(THIRDPARTY_DEPS)/cares.dep \
	$(THIRDPARTY_DEPS)/protobuf.dep \
	$(THIRDPARTY_DEPS)/cotire.dep \
	$(THIRDPARTY_DEPS)/spdlog.dep \
	$(THIRDPARTY_DEPS)/http-parser.dep \
	$(THIRDPARTY_DEPS)/tclap.dep \
	$(THIRDPARTY_DEPS)/lightstep.dep \
	$(THIRDPARTY_DEPS)/rapidjson.dep \
	$(THIRDPARTY_DEPS)/googletest.dep \
	$(THIRDPARTY_DEPS)/gcovr.dep

RECIPES := build_recipes

DISTINCT_PATH = $(if $(BUILD_DISTINCT),$(@F),)

build-setup = rm -rf $@.build && \
	$(if $(BUILD_DISTINCT),rm -rf $(THIRDPARTY_BUILD)/$(DISTINCT_PATH) &&,) \
	$(if $(BUILD_DISTINCT),rm -rf $(THIRDPARTY_SRC)/$(DISTINCT_PATH) &&,) \
	mkdir -p $@.build && \
	mkdir -p $(THIRDPARTY_BUILD)/$(DISTINCT_PATH)/lib && \
	mkdir -p $(THIRDPARTY_BUILD)/$(DISTINCT_PATH)/include && \
	cd $@.build && \
	echo "Building in $@.build, logs at $@.log"

build-complete = rm -rf $@.build && \
	echo "Successful build of $@" && \
	touch $@

build-recipe = cd $(THIRDPARTY_SRC) && \
    $(build-setup) && \
	(((THIRDPARTY_SRC=$(THIRDPARTY_SRC)/$(DISTINCT_PATH) \
	   THIRDPARTY_BUILD=$(THIRDPARTY_BUILD)/$(DISTINCT_PATH) \
	   $(1) \
	   bash $(realpath $<) 2>&1) > $@.log) || (cat $@.log; exit 1)) && \
	$(build-complete)

$(THIRDPARTY_DEPS)/%.dep: $(RECIPES)/%.sh
	@+$(call build-recipe,)

# Special support for targets that need protobuf, and hence take a dependency on protobuf.dep.
PROTOBUF_BUILD ?= $(THIRDPARTY_BUILD)/$(if $(BUILD_DISTINCT),protobuf.dep,)

$(THIRDPARTY_DEPS)/lightstep.dep: $(RECIPES)/lightstep.sh $(THIRDPARTY_DEPS)/protobuf.dep
	@+$(call build-recipe,PROTOBUF_BUILD=$(PROTOBUF_BUILD))
