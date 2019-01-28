#! /usr/bin/env python

from __future__ import print_function

import argparse
import os
import re
import subprocess
import sys

# TODO(zuercher): provide support for fixing errors

TOOLS_DIR = os.path.dirname(os.path.realpath(__file__))

# e.g., // comment OR /* comment */ (single line)
# Limit the characters that may precede // to help filter out some code
# mistakenly processed as a comment.
INLINE_COMMENT = re.compile(r'(?:^|[^:"])//(.*?)$|/\*+(.*?)\*+/')

# e.g., /* comment */ (multiple lines)
MULTI_COMMENT_START = re.compile(r'/\*(.*?)$')
MULTI_COMMENT_END = re.compile(r'^(.*?)\*/')

# e.g., TODO(username): blah
TODO = re.compile(r'(TODO|NOTE)\s*\(@?[A-Za-z0-9-]+\):?')

# e.g., ignore parameter names in doxygen comments
METHOD_DOC = re.compile('@(param\s+\w+|return(\s+const)?\s+\w+)')

# Camel Case splitter
CAMEL_CASE = re.compile(r'[A-Z]?[a-z]+|[A-Z]+(?=[A-Z]|$)')

# Base64: we assume base64 encoded data in tests is never mixed with
# other comments on a single line.
BASE64 = re.compile(r'^[\s*]+([A-Za-z0-9/+=]{16,})\s*$')
NUMBER = re.compile(r'\d')

# Hex: match 1) longish strings of hex digits (to avoid matching "add" and
# other simple words that happen to look like hex), 2) 2 or more two digit
# hex numbers separated by colons, 3) "0x" prefixed hex numbers of any length,
# or 4) UUIDs
HEX = re.compile(r'(?:^|\s|[(])([A-Fa-f0-9]{8,})(?:$|\s|[.,)])')
HEX_SIG = re.compile(r'\W([A-Fa-f0-9]{2}(:[A-Fa-f0-9]{2})+)\W')
PREFIXED_HEX = re.compile(r'0x[A-Fa-f0-9]+')
UUID = re.compile(r'[A-Fa-f0-9]{8}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{12}')

# Matches e.g. FC00::/8 or 2001::abcd/64. Does not match ::1/128, but
# aspell ignores that anyway.
IPV6_ADDR = re.compile(r'\W([A-Fa-f0-9]+:[A-Fa-f0-9:]+/[0-9]{1,3})\W')

# Quoted words: "word", 'word', or *word*
QUOTED_WORD = re.compile(r'(["\'*])[A-Za-z0-9]+(\1)')

# Command flags (e.g. "-rf") and percent specifiers
FLAG = re.compile(r'\W([-%][A-Za-z]+)')

# Bare github users (e.g. @user)
USER = re.compile(r'\W(@[A-Za-z0-9-]+)')

DEBUG = False
COLOR = True
MARK = False


def red(s):
  if COLOR:
    return "\33[1;31m" + s + "\033[0m"
  return s


def debug(s):
  if DEBUG:
    print(s)


# Split camel case words and run them through the dictionary. Returns
# True if they are all spelled correctly, False if word is not camel
# case or has a misspelled sub-word.
def check_camel_case(aspell, word):
  # Words is not camel case: the previous result stands.
  parts = re.findall(CAMEL_CASE, word)
  if len(parts) <= 1:
    return False

  for part in parts:
    if check_comment(aspell, 0, part):
      # Part of camel case word is misspelled, the result stands.
      return False

  return True


# Find occurrences of the regex within comment and replace the numbered
# matching group with spaces. If secondary is defined, the matching
# group must also match secondary to be masked.
def mask_with_regex(comment, regex, group, secondary=None):
  for m in regex.finditer(comment):
    if secondary and secondary.search(m.group(group)) is None:
      continue

    start = m.start(group)
    end = m.end(group)
    comment = comment[:start] + (' ' * (end - start)) + comment[end:]

  return comment


# Checks the comment at offset against the aspell pipe. Result is an array
# of tuples where each tuple is the misspelled word, it's offset from the
# start of the line, and an array of possible replacements.
def check_comment(aspell, offset, comment):
  # Replace TODO comments with spaces to preserve string offsets.
  comment = mask_with_regex(comment, TODO, 0)

  # Ignore @param varname
  comment = mask_with_regex(comment, METHOD_DOC, 0)

  # Similarly, look for base64 sequences, but they must have at least one
  # digit.
  comment = mask_with_regex(comment, BASE64, 1, NUMBER)

  # Various hex constants:
  comment = mask_with_regex(comment, HEX, 1)
  comment = mask_with_regex(comment, HEX_SIG, 1)
  comment = mask_with_regex(comment, PREFIXED_HEX, 0)
  comment = mask_with_regex(comment, UUID, 0)
  comment = mask_with_regex(comment, IPV6_ADDR, 1)

  # Single words in quotes:
  comment = mask_with_regex(comment, QUOTED_WORD, 0)

  # Command flags:
  comment = mask_with_regex(comment, FLAG, 1)

  # Github user refs:
  comment = mask_with_regex(comment, USER, 1)

  # Everything got stripped, return early.
  if comment == "" or comment.strip() == "":
    return []

  # aspell does not like leading punctuation
  if not comment[0].isalnum():
    comment = ' ' + comment[1:]

  errors = []

  aspell.poll()
  if aspell.returncode is not None:
    print("aspell quit unexpectedly: return code %d" % (aspell.returncode))
    sys.exit(1)

  debug("ASPELL< %s" % (comment))

  aspell.stdin.write(comment + os.linesep)
  aspell.stdin.flush()
  while True:
    result = aspell.stdout.readline().strip()
    debug("ASPELL> %s" % (result))

    if result == "":
      break  # handled all results

    t = result[0]
    if t == "*" or t == "-" or t == "+":
      # *: found in dictionary
      # -: found run-together words in dictionary
      # +: found root word in dictionary
      continue

    # & <original> <N> <offset>: m1, m2, ... mN, g1, g2, ...
    # ? <original> 0 <offset>: g1, g2, ....
    # # <original> <offset>
    original, rem = result[2:].split(" ", 1)

    if t == "#":
      # Not in dictionary, but no suggestions
      errors.append((original, int(rem) + offset, []))
    elif t == '&' or t == '?':
      # Near misses and/or guesses
      _, rem = rem.split(" ", 1)  # drop N (or 0)
      o, rem = rem.split(": ", 1)  # o is offset from start of comment
      suggestions = rem.split(", ")

      errors.append((original, int(o) + offset, suggestions))
    else:
      print("aspell produced unexpected output: %s" % (result))
      sys.exit(2)

  # Retry camel case words after splitting them.
  errors = [err for err in errors if not check_camel_case(aspell, err[0])]
  return errors


def check_file(aspell, file, lines):
  in_comment = False
  line_num = 0
  num = 0
  for line in lines:
    line_num += 1
    errors = []
    last = 0
    if in_comment:
      mc_end = MULTI_COMMENT_END.search(line)
      if mc_end is None:
        # Full line is within a multi-line comment.
        errors += check_comment(aspell, 0, line)
        num += 1
      else:
        # Start of line is the end of a multi-line comment.
        errors += check_comment(aspell, 0, mc_end.group(1))
        num += 1
        last = mc_end.end()
        in_comment = False

    if not in_comment:
      for inline in INLINE_COMMENT.finditer(line, last):
        # Single-line comment
        m = inline.lastindex  # 1 or 2 depending on group matched
        errors += check_comment(aspell, inline.start(m), inline.group(m))
        num += 1
        last = inline.end(m)
      if last < len(line):
        mc_start = MULTI_COMMENT_START.search(line, last)
        if mc_start is not None:
          # New multi-lie comment starts at end of line.
          errors += check_comment(aspell, mc_start.start(1), mc_start.group(1))
          num += 1
          in_comment = True

    if errors:
      # Highlight misspelled words.
      prefix = "%s:%d:" % (file, line_num)
      for (word, offset, suggestions) in reversed(errors):
        line = line[:offset] + red(word) + line[offset + len(word):]

      print("%s%s" % (prefix, line.rstrip()))

      if MARK:
        # Print a caret at the start of each misspelled word.
        marks = ' ' * len(prefix)
        last = 0
        for (word, offset, suggestions) in errors:
          marks += (' ' * (offset - last)) + '^'
          last = offset + 1
        print(marks)

  return num


def start_aspell(dictionary):
  # Read the custom dictionary
  words = []
  with open(dictionary, 'r') as f:
    words = f.readlines()

  # Strip comments.
  words = [w for w in words if len(w) > 0 and w[0] != "#"]

  # Allow acronyms and abbreviations to be spelled in lowercase.
  # (e.g. Convert "HTTP"" into "HTTP" and "http" which also matches
  # "Http").
  for word in words:
    if word.isupper():
      words += word.lower()

  # Generate aspell personal dictionary.
  pws = os.path.join(TOOLS_DIR, '.aspell.en.pws')
  with open(pws, 'w') as f:
    f.write("personal_ws-1.1 en %d\n" % (len(words)))
    for word in words:
      f.write(word)

  # Start an aspell process.
  aspell_args = ["aspell", "pipe", "--run-together", "--encoding=utf-8", "--personal=" + pws]
  aspell = subprocess.Popen(
      aspell_args,
      bufsize=4096,
      stdin=subprocess.PIPE,
      stdout=subprocess.PIPE,
      stderr=subprocess.STDOUT,
      universal_newlines=True)

  # Read the version line that aspell emits on startup.
  aspell.stdout.readline()
  return aspell


def execute(files, dictionary):
  aspell = start_aspell(dictionary)

  num = 0
  for path in files:
    with open(path, 'r') as f:
      lines = f.readlines()
      num += check_file(aspell, path, lines)

  aspell.stdin.close()
  aspell.wait()

  print("Checked %d lines of comments" % (num))


if __name__ == "__main__":
  default_dictionary = os.path.join(TOOLS_DIR, 'spelling_dictionary.txt')

  parser = argparse.ArgumentParser(description="Check comment spelling.")
  parser.add_argument(
      'operation_type',
      type=str,
      choices=['check'],
      help="specify if the run should 'check' or 'fix' spelling.")
  parser.add_argument(
      'target_paths', type=str, nargs="*", help="specify the files for the script to process.")
  parser.add_argument('-d', '--debug', action='store_true', help="Debug spell checker subprocess.")
  parser.add_argument(
      '--mark', action='store_true', help="Emits extra output to mark misspelled words.")
  parser.add_argument(
      '--dictionary',
      type=str,
      default=default_dictionary,
      help="specify a location for Envoy-specific dictionary words")
  parser.add_argument(
      '--color',
      type=str,
      choices=['on', 'off', 'auto'],
      default="auto",
      help="Controls colorized output. Auto limits color to TTY devices.")
  args = parser.parse_args()

  COLOR = args.color == "on" or (args.color == "auto" and sys.stdout.isatty())
  DEBUG = args.debug
  MARK = args.mark

  target_paths = args.target_paths
  if not target_paths:
    exts = ['.cc', '.h', '.proto']
    target_paths = []
    paths = ['./api', './include', './source', './test']
    for p in paths:
      for root, _, files in os.walk(p):
        target_paths += [os.path.join(root, f) for f in files if os.path.splitext(f)[1] in exts]

  execute(target_paths, args.dictionary)
