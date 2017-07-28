#!/usr/bin/env python

# This tool takes an ELF binary that has been built with -Wl,--build-id=md5'
# '-Wl,--hash-style=gnu (as done by Bazel prior to
# https://github.com/bazelbuild/bazel/commit/724706ba4836c3366fc85b40ed50ccf92f4c3882,
# versions prior to 0.5), and replaces the MD5 compiler hash with a truncated
# git SHA1 hash found in Envoy's version_generated.cc.
#
# This is useful to folks who want the build commit in the .note.gnu.build-id
# section rather than the compiler hash of inputs. Please note that the hash is
# a 16 byte truncated git SHA1, rather than a complete 20 byte git SHA1.
# This is a workaround to https://github.com/bazelbuild/bazel/issues/2805.

import binascii
import platform
import re
import subprocess as sp
import sys

# This is what the part of .note.gnu.build-id prior to the MD5 hash looks like.
EXPECTED_BUILD_ID_NOTE_PREFIX = [
    # The "name" of the note is 4 bytes long.
    0x04,
    0x00,
    0x00,
    0x00,
    # The "description" of the note is 16 bytes. 
    0x10,
    0x00,
    0x00,
    0x00,
    # The "type" of the note.
    0x03,
    0x00,
    0x00,
    0x00,
    # 'G', 'N', 'U', '\0' (name)
    0x47,
    0x4e,
    0x55,
    0x00,
]
# We're expecting an MD5 hash, 16 bytes.
MD5_HASH_LEN = 16
SHA1_HASH_LEN = 20
EXPECTED_BUILD_ID_NOTE_LENGTH = len(EXPECTED_BUILD_ID_NOTE_PREFIX) + MD5_HASH_LEN


class RewriterException(Exception):
  pass


# Extract git SHA1 hash from "envoy --version" stdout.
def ExtractGitSha(path):
  version_output = sp.check_output([path, '--version']).strip()
  sr = re.search('version: (\w+)/', version_output)
  if not sr:
    raise RewriterException('Bad envoy --version: %s' % version_output)
  sha1_hash = sr.group(1)
  if len(sha1_hash) != 2 * SHA1_HASH_LEN:
    raise RewriterException('Bad SHA1 hash in %s: %s' % (path, sha1_hash))
  return sha1_hash


# Scrape the offset of .note.gnu.build-id via readelf from the binary. Also
# verify the note section is what we expect.
def ExtractBuildIdNoteOffset(path):
  try:
    readelf_output = sp.check_output('readelf -SW %s' % path, shell=True)
    # Sanity check the ordering of fields from readelf.
    if not re.search('Name\s+Type\s+Address\s+Off\s+Size\s', readelf_output):
      raise RewriterException('Invalid readelf output: %s' % readelf_output)
    sr = re.search('.note.gnu.build-id\s+NOTE\s+\w+\s+(\w+)\s(\w+)\s',
                   readelf_output)
    if not sr:
      raise RewriterException(
          'Unable to parse .note.gnu.build-id note: %s' % readelf_output)
    raw_note_offset, raw_note_size = sr.groups()
    if long(raw_note_size, 16) != EXPECTED_BUILD_ID_NOTE_LENGTH:
      raise RewriterException(
          'Incorrect .note.gnu.build-id note size: %s' % readelf_output)
    note_offset = long(raw_note_offset, 16)
    with open(path, 'rb') as f:
      f.seek(note_offset)
      note_prefix = [ord(b) for b in f.read(len(EXPECTED_BUILD_ID_NOTE_PREFIX))]
      if note_prefix != EXPECTED_BUILD_ID_NOTE_PREFIX:
        raise RewriterException(
            'Unexpected .note.gnu.build-id prefix in %s: %s' % (path,
                                                                note_prefix))
    return note_offset
  except sp.CalledProcessError as e:
    raise RewriterException('%s %s' % (e, readelf_output.output))


# Inplace binary rewriting of the 16 byte .note.gnu.build-id description with
# the truncated hash.
def RewriteBinary(path, offset, git5_sha1):
  truncated_hash = git5_sha1[:2 * MD5_HASH_LEN]
  print 'Writing %s truncated to %s at offset 0x%x in %s' % (git5_sha1,
                                                             truncated_hash,
                                                             offset, path)
  with open(path, 'r+b') as f:
    f.seek(offset + len(EXPECTED_BUILD_ID_NOTE_PREFIX))
    f.write(binascii.unhexlify(truncated_hash))


if __name__ == '__main__':
  if len(sys.argv) != 2:
    print 'Usage: %s <Envoy binary path> ' % sys.argv[0]
    sys.exit(1)
  if platform.system() == 'Darwin':
    print 'Stamping not supported for Mach-O binaries'
    sys.exit(0)
  envoy_bin_path = sys.argv[1]
  version_generated = ExtractGitSha(envoy_bin_path)
  build_id_note_offset = ExtractBuildIdNoteOffset(envoy_bin_path)
  RewriteBinary(envoy_bin_path, build_id_note_offset, version_generated)
