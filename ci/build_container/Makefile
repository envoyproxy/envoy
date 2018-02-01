# The individual build recipe scripts must contain sufficient information (e.g.  SHA, URL, repo,
# version number, etc.) to uniquely identify the revision of the upstream dependency.  This allows
# make to pick up changes with a simple direct dependency on the build recipe.

RECIPES := build_recipes

# Make sure we use a consistent compiler across all deps.
CC ?= gcc
CXX ?= g++

# Common compiler flags
CXXFLAGS ?= -ggdb3 -fno-omit-frame-pointer -O2
CFLAGS ?= -ggdb3 -fno-omit-frame-pointer -O2
CPPFLAGS ?= -DNDEBUG

# If $(BUILD_DISTINCT) is set in the make environment, the artifacts are built and installed in
# distinct directories under $(THIRDPARTY_BUILD) and $(THIRDPARTY_SRC). They end up looking like
# $(THIRDPARTY_BUILD)/protobuf.dep/include, etc. instead of all being under
# $(THIRDPARTY_BUILD)/include.
DISTINCT_PATH = $(if $(BUILD_DISTINCT),$(@F),)

build-setup = rm -rf "$@.build" && \
	$(if $(BUILD_DISTINCT),rm -rf "$(THIRDPARTY_BUILD)/$(DISTINCT_PATH)" &&,) \
	$(if $(BUILD_DISTINCT),rm -rf "$(THIRDPARTY_SRC)/$(DISTINCT_PATH)" &&,) \
	mkdir -p "$@.build" && \
	mkdir -p "$(THIRDPARTY_BUILD)/$(DISTINCT_PATH)/lib" && \
	mkdir -p "$(THIRDPARTY_BUILD)/$(DISTINCT_PATH)/include" && \
	cd "$@.build" && \
	echo "Building in $@.build, logs at $@.log"

build-complete = rm -rf "$@.build" && \
	echo "Successful build of $@" && \
	touch $@

# This needs to be invoked with $(call build-recipe,DEFS) where DEFS are additional environment
# definitions that are to be injected into the build recipe execution environment.
build-recipe = cd "$(THIRDPARTY_SRC)" && \
    $(build-setup) && \
	(((THIRDPARTY_SRC="$(THIRDPARTY_SRC)/$(DISTINCT_PATH)" \
	   THIRDPARTY_BUILD="$(THIRDPARTY_BUILD)/$(DISTINCT_PATH)" \
	   CC="$(CC)" \
	   CXX="$(CXX)" \
	   CFLAGS="$(CFLAGS)" \
	   CXXFLAGS="$(CXXFLAGS)" \
	   CPPFLAGS="$(CPPFLAGS)" \
	   $(1) \
	   bash -c "time $(CURDIR)/recipe_wrapper.sh $(realpath $<)" 2>&1) > $@.log) || (cat $@.log; exit 1)) && \
	$(build-complete)

$(THIRDPARTY_DEPS)/%.dep: $(RECIPES)/%.sh
	@+$(call build-recipe,)

# Special support for targets that need protobuf, and hence take a dependency on protobuf.dep.
PROTOBUF_BUILD ?= $(THIRDPARTY_BUILD)/$(if $(BUILD_DISTINCT),protobuf.dep,)
