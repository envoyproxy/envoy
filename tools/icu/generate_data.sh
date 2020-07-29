#!/bin/bash

set -u
set -e

# Latest version: 23 April 2020, https://github.com/unicode-org/icu/releases/tag/release-67-1.
# https://github.com/unicode-org/icu/archive/release-67-1.tar.gz
VERSION="67.1"
VERSION_MAJOR="67"
SRC_SHA="c664ba23be70284d0539e2018a44ed7956e6d3bd"
TMP_DIR="/tmp"

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

SRC_FILENAME="release-"${VERSION//./-}".tar.gz"
if [[ ! -e "${TMP_DIR}/${SRC_FILENAME}" ]]; then
  if ! wget https://github.com/unicode-org/icu/archive/"${SRC_FILENAME}" \
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

tar -xvf "${TMP_DIR}"/"${SRC_FILENAME}" -C "${TMP_DIR}" &> /dev/null

WORK_DIR="${TMP_DIR}"/icu-release-"${VERSION//./-}"/icu4c/source
LIB_DIR="${WORK_DIR}"/lib

cp -f "${ROOTDIR}"/tools/icu/filters.json "${WORK_DIR}"

pushd "${WORK_DIR}"

# Filter the required ICU data.
ICU_DATA_FILTER_FILE="${WORK_DIR}"/filters.json ./runConfigureICU "${OS}"

# Build the data.
make clean && make -j 16

pushd "${WORK_DIR}"/data/out/tmp

# Generate *.c from *.dat.
DYLD_LIBRARY_PATH="${LIB_DIR}" \
  LD_LIBRARY_PATH="${LIB_DIR}" \
  "${WORK_DIR}"/bin/genccode "icudt"${VERSION_MAJOR}l.dat

echo "U_CAPI const void * U_EXPORT2 uprv_getICUData_conversion() { return icudt${VERSION_MAJOR}l_dat.bytes; }" \
  >> icudt${VERSION_MAJOR}l_dat.c

rm -f ${ROOTDIR}/bazel/external/icu/data/data.c.gz.*

gzip -c --best "${WORK_DIR}"/data/out/tmp/icudt${VERSION_MAJOR}l_dat.c \
  | split -b 1048576 - ${ROOTDIR}/bazel/external/icu/data/data.c.gz.

popd
