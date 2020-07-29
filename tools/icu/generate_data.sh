#!/bin/bash

set -u
set -e

# Latest version: 23 April 2020, https://github.com/unicode-org/icu/releases/tag/release-67-1.
VERSION="67.1"
VERSION_MAJOR="67"
SRC_SHA="6822a4a94324d1ba591b3e8ef084e4491af253c1"
TMP_DIR="/tmp"
LIB_DIR="${TMP_DIR}"/icu/source/lib

# node.js ICU_DATA_FILTER_FILE. The "small" (English only) ICU (https://github.com/nodejs/node/tree/634a9a97f4b380390352543452aed6c7c9defcb4/tools/icu).
ICU_DATA_FILTER_FILE="https://raw.githubusercontent.com/nodejs/node/634a9a97f4b380390352543452aed6c7c9defcb4/tools/icu/icu_small.json"
ICU_DATA_FILTER_FILE_SHA="5da23ef2ffa783056fd241585439d41b57c6f0ec"

if [[ "$(uname)" == "Darwin" ]]; then
  OS="MacOSX"
elif [[ "$(uname)" == "Linux" ]]; then
  OS="Linux"
else
  echo "Current only support macOS/Linux"
  exit 1
fi

SCRIPTPATH=$( cd "$(dirname "$0")" ; pwd -P )
ROOTDIR="${SCRIPTPATH}/../.."
cd "$ROOTDIR"

SRC_FILENAME="icu4c-"${VERSION//./_}"-src.tgz"
if [[ ! -e "${TMP_DIR}/${SRC_FILENAME}" ]]; then
  if ! wget https://github.com/unicode-org/icu/releases/download/release-"${VERSION//./-}"/icu4c-"${VERSION//./_}"-src.tgz \
    -O "${TMP_DIR}/${SRC_FILENAME}" --no-verbose --tries=3 -o "${TMP_DIR}/wget.log"; then
    cat "${TMP_DIR}/wget.log"
    exit -1
  fi
fi

ACTUAL_SHA=""
EXPECT_SHA=""

if [[ "${OS}" == "Linux" ]]; then
  ACTUAL_SHA=$(sha1sum "${TMP_DIR}/${SRC_FILENAME}"|cut -d' ' -f1)
  EXPECT_SHA="${SRC_SHA}"
else
  ACTUAL_SHA=$(shasum -a 1 "${TMP_DIR}/${SRC_FILENAME}"|cut -d' ' -f1)
  EXPECT_SHA="${SRC_SHA}"
fi

if [[ ! ${ACTUAL_SHA} == ${EXPECT_SHA} ]]; then
   echo "Expect shasum is ${ACTUAL_SHA}, but actual is shasum ${EXPECT_SHA}"
   exit 1
fi

tar -xvf "${TMP_DIR}/${SRC_FILENAME}" -C "${TMP_DIR}" &> /dev/null

if [[ ! -e "${TMP_DIR}"/icu/source/filters.json ]]; then
  if ! wget "${ICU_DATA_FILTER_FILE}" \
  -O "${TMP_DIR}"/icu/source/filters.json --no-verbose --tries=3 -o "${TMP_DIR}/wget.log"; then
    cat "${TMP_DIR}/wget.log"
    exit -1
  fi
fi

ACTUAL_SHA=""
EXPECT_SHA=""

if [[ "${OS}" == "Linux" ]]; then
  ACTUAL_SHA=$(sha1sum "${TMP_DIR}"/icu/source/filters.json|cut -d' ' -f1)
  EXPECT_SHA="${ICU_DATA_FILTER_FILE_SHA}"
else
  ACTUAL_SHA=$(shasum -a 1 "${TMP_DIR}"/icu/source/filters.json|cut -d' ' -f1)
  EXPECT_SHA="${ICU_DATA_FILTER_FILE_SHA}"
fi

if [[ ! ${ACTUAL_SHA} == ${EXPECT_SHA} ]]; then
   echo "Expect shasum is ${ACTUAL_SHA}, but actual is shasum ${EXPECT_SHA}"
   exit 1
fi

pushd "${TMP_DIR}"/icu/source

# Filter the required ICU data.
ICU_DATA_FILTER_FILE="${TMP_DIR}"/icu/source/filters.json ./runConfigureICU "${OS}"

# Build the data.
make clean && make

pushd "${TMP_DIR}"/icu/source/data/out/tmp

# Generate *.c from *.dat.
DYLD_LIBRARY_PATH="${LIB_DIR}" \
  LD_LIBRARY_PATH="${LIB_DIR}" \
  ${TMP_DIR}"/icu/source/bin/genccode" "icudt"${VERSION_MAJOR}l.dat

echo "U_CAPI const void * U_EXPORT2 uprv_getICUData_conversion() { return icudt${VERSION_MAJOR}l_dat.bytes; }" \
  >> icudt${VERSION_MAJOR}l_dat.c

gzip -c --best "${TMP_DIR}"/icu/source/data/out/tmp/icudt${VERSION_MAJOR}l_dat.c \
  | split -b 1048576 - ${ROOTDIR}/bazel/external/icu/data/data.c.gz.

popd
popd
