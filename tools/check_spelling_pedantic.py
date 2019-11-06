#! /usr/bin/env python3

from __future__ import print_function

import argparse
import locale
import math
import os
import re
import subprocess
import sys

from functools import partial
from itertools import chain

# Handle function rename between python 2/3.
try:
  input = raw_input
except NameError:
  pass

try:
  cmp
except NameError:

  def cmp(x, y):
    return (x > y) - (x < y)


TOOLS_DIR = os.path.dirname(os.path.realpath(__file__))

# Single line comments: // comment OR /* comment */
# Limit the characters that may precede // to help filter out some code
# mistakenly processed as a comment.
INLINE_COMMENT = re.compile(r'(?:^|[^:"])//(.*?)$|/\*+(.*?)\*+/')

# Multi-line comments: /* comment */ (multiple lines)
MULTI_COMMENT_START = re.compile(r'/\*(.*?)$')
MULTI_COMMENT_END = re.compile(r'^(.*?)\*/')

# Envoy TODO comment style.
TODO = re.compile(r'(TODO|NOTE)\s*\(@?[A-Za-z0-9-]+\):?')

# Ignore parameter names in doxygen comments.
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
# or 4) UUIDs.
HEX = re.compile(r'(?:^|\s|[(])([A-Fa-f0-9]{8,})(?:$|\s|[.,)])')
HEX_SIG = re.compile(r'\W([A-Fa-f0-9]{2}(:[A-Fa-f0-9]{2})+)\W')
PREFIXED_HEX = re.compile(r'0x[A-Fa-f0-9]+')
UUID = re.compile(r'[A-Fa-f0-9]{8}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{12}')
BIT_FIELDS = re.compile(r'[01]+[XxYy]+')
AB_FIELDS = re.compile(r'\W([AB]+)\W')

# Matches e.g. FC00::/8 or 2001::abcd/64. Does not match ::1/128, but
# aspell ignores that anyway.
IPV6_ADDR = re.compile(r'\W([A-Fa-f0-9]+:[A-Fa-f0-9:]+/[0-9]{1,3})\W')

# Quoted words: "word", 'word', or *word*.
QUOTED_WORD = re.compile(r'(["\'*])[A-Za-z0-9]+(\1)')

# Command flags (e.g. "-rf") and percent specifiers.
FLAG = re.compile(r'\W([-%][A-Za-z]+)')

# Bare github users (e.g. @user).
USER = re.compile(r'\W(@[A-Za-z0-9-]+)')

# Valid dictionary words. Anything else crashes aspell.
DICTIONARY_WORD = re.compile(r"^[A-Za-z']+$")

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


class SpellChecker:
  """Aspell-based spell checker."""

  def __init__(self, dictionary_file):
    self.dictionary_file = dictionary_file
    self.aspell = None

  def start(self):
    words = self.load_dictionary()

    # Generate aspell personal dictionary.
    pws = os.path.join(TOOLS_DIR, '.aspell.en.pws')
    with open(pws, 'w') as f:
      f.write("personal_ws-1.1 en %d\n" % (len(words)))
      f.writelines(words)

    # Start an aspell process.
    aspell_args = ["aspell", "pipe", "--lang=en_US", "--encoding=utf-8", "--personal=" + pws]
    self.aspell = subprocess.Popen(aspell_args,
                                   bufsize=4096,
                                   stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT,
                                   universal_newlines=True)

    # Read the version line that aspell emits on startup.
    self.aspell.stdout.readline()

  def stop(self):
    if not self.aspell:
      return

    self.aspell.stdin.close()
    self.aspell.wait()
    self.aspell = None

  def check(self, line):
    self.aspell.poll()
    if self.aspell.returncode is not None:
      print("aspell quit unexpectedly: return code %d" % (self.aspell.returncode))
      sys.exit(2)

    debug("ASPELL< %s" % (line))

    self.aspell.stdin.write(line + os.linesep)
    self.aspell.stdin.flush()

    errors = []
    while True:
      result = self.aspell.stdout.readline().strip()
      debug("ASPELL> %s" % (result))

      # Check for end of results.
      if result == "":
        break

      t = result[0]
      if t == "*" or t == "-" or t == "+":
        # *: found in dictionary.
        # -: found run-together words in dictionary.
        # +: found root word in dictionary.
        continue

      # & <original> <N> <offset>: m1, m2, ... mN, g1, g2, ...
      # ? <original> 0 <offset>: g1, g2, ....
      # # <original> <offset>
      original, rem = result[2:].split(" ", 1)

      if t == "#":
        # Not in dictionary, but no suggestions.
        errors.append((original, int(rem), []))
      elif t == '&' or t == '?':
        # Near misses and/or guesses.
        _, rem = rem.split(" ", 1)  # Drop N (may be 0).
        o, rem = rem.split(": ", 1)  # o is offset from start of line.
        suggestions = rem.split(", ")

        errors.append((original, int(o), suggestions))
      else:
        print("aspell produced unexpected output: %s" % (result))
        sys.exit(2)

    return errors

  def load_dictionary(self):
    # Read the custom dictionary.
    words = []
    with open(self.dictionary_file, 'r') as f:
      words = f.readlines()

    # Strip comments, invalid words, and blank lines.
    words = [w for w in words if len(w.strip()) > 0 and re.match(DICTIONARY_WORD, w)]

    # Allow acronyms and abbreviations to be spelled in lowercase.
    # (e.g. Convert "HTTP" into "HTTP" and "http" which also matches
    # "Http").
    for word in words:
      if word.isupper():
        words += word.lower()

    return words

  def add_words(self, additions):
    lines = []
    with open(self.dictionary_file, 'r') as f:
      lines = f.readlines()

    additions = [w + os.linesep for w in additions]
    additions.sort()

    # Insert additions into the lines ignoring comments and blank lines.
    idx = 0
    add_idx = 0
    while idx < len(lines) and add_idx < len(additions):
      line = lines[idx]
      if len(line.strip()) != 0 and line[0] != "#":
        c = cmp(additions[add_idx], line)
        if c < 0:
          lines.insert(idx, additions[add_idx])
          add_idx += 1
        elif c == 0:
          add_idx += 1
      idx += 1

    # Append any remaining additions.
    lines += additions[add_idx:]

    with open(self.dictionary_file, 'w') as f:
      f.writelines(lines)

    self.stop()
    self.start()


# Split camel case words and run them through the dictionary. Returns
# a replacement list of errors. The replacement list may contain just
# the original error (if the word is not camel case), may be empty if
# the split words are all spelled correctly, or may be a new set of
# errors referencing the misspelled sub-words.
def check_camel_case(checker, err):
  (word, word_offset, _) = err

  debug("check camel case %s" % (word))
  parts = re.findall(CAMEL_CASE, word)

  # Word is not camel case: the previous result stands.
  if len(parts) <= 1:
    debug("  -> not camel case")
    return [err]

  split_errs = []
  part_offset = 0
  for part in parts:
    debug("  -> part: %s" % (part))
    split_err = checker.check(part)
    if split_err:
      debug("    -> not found in dictionary")
      split_errs += [(part, word_offset + part_offset, split_err[0][2])]
    part_offset += len(part)

  return split_errs


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


# Checks the comment at offset against the spell checker. Result is an array
# of tuples where each tuple is the misspelled word, it's offset from the
# start of the line, and an array of possible replacements.
def check_comment(checker, offset, comment):
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
  comment = mask_with_regex(comment, BIT_FIELDS, 0)
  comment = mask_with_regex(comment, AB_FIELDS, 1)
  comment = mask_with_regex(comment, UUID, 0)
  comment = mask_with_regex(comment, IPV6_ADDR, 1)

  # Single words in quotes:
  comment = mask_with_regex(comment, QUOTED_WORD, 0)

  # Command flags:
  comment = mask_with_regex(comment, FLAG, 1)

  # Github user refs:
  comment = mask_with_regex(comment, USER, 1)

  # Everything got masked, return early.
  if comment == "" or comment.strip() == "":
    return []

  # Mask leading punctuation.
  if not comment[0].isalnum():
    comment = ' ' + comment[1:]

  errors = checker.check(comment)

  # Fix up offsets relative to the start of the line vs start of the comment.
  errors = [(w, o + offset, s) for (w, o, s) in errors]

  # CamelCase words get split and re-checked
  errors = [*chain.from_iterable(map(lambda err: check_camel_case(checker, err), errors))]

  return errors


def print_error(file, line_offset, lines, errors):
  # Highlight misspelled words.
  line = lines[line_offset]
  prefix = "%s:%d:" % (file, line_offset + 1)
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


def print_fix_options(word, suggestions):
  print("%s:" % (word))
  print("  a: accept and add to dictionary")
  print("  A: accept and add to dictionary as ALLCAPS (for acronyms)")
  print("  f <word>: replace with the given word without modifying dictionary")
  print("  i: ignore")
  print("  r <word>: replace with given word and add to dictionary")
  print("  R <word>: replace with given word and add to dictionary as ALLCAPS (for acronyms)")
  print("  x: abort")

  if not suggestions:
    return

  col_width = max(len(word) for word in suggestions)
  opt_width = int(math.log(len(suggestions), 10)) + 1
  padding = 2  # Two spaces of padding.
  delim = 2  # Colon and space after number.
  num_cols = int(78 / (col_width + padding + opt_width + delim))
  num_rows = int(len(suggestions) / num_cols + 1)
  rows = [""] * num_rows

  indent = " " * padding
  for idx, sugg in enumerate(suggestions):
    row = idx % len(rows)
    row_data = "%d: %s" % (idx, sugg)

    rows[row] += indent + row_data.ljust(col_width + opt_width + delim)

  for row in rows:
    print(row)


def fix_error(checker, file, line_offset, lines, errors):
  print_error(file, line_offset, lines, errors)

  fixed = {}
  replacements = []
  additions = []
  for (word, offset, suggestions) in errors:
    if word in fixed:
      # Same typo was repeated in a line, so just reuse the previous choice.
      replacements += [fixed[word]]
      continue

    print_fix_options(word, suggestions)

    replacement = ""
    while replacement == "":
      try:
        choice = input("> ")
      except EOFError:
        choice = "x"

      add = None
      if choice == "x":
        print("Spell checking aborted.")
        sys.exit(2)
      elif choice == "a":
        replacement = word
        add = word
      elif choice == "A":
        replacement = word
        add = word.upper()
      elif choice[:1] == "f":
        replacement = choice[1:].strip()
        if replacement == "":
          print("Invalid choice: '%s'. Must specify a replacement (e.g. 'f corrected')." % (choice))
          continue
      elif choice == "i":
        replacement = word
      elif choice[:1] == "r" or choice[:1] == "R":
        replacement = choice[1:].strip()
        if replacement == "":
          print("Invalid choice: '%s'. Must specify a replacement (e.g. 'r corrected')." % (choice))
          continue

        if choice[:1] == "R":
          if replacement.upper() not in suggestions:
            add = replacement.upper()
        elif replacement not in suggestions:
          add = replacement
      else:
        try:
          idx = int(choice)
        except ValueError:
          idx = -1
        if idx >= 0 and idx < len(suggestions):
          replacement = suggestions[idx]
        else:
          print("Invalid choice: '%s'" % (choice))

    fixed[word] = replacement
    replacements += [replacement]
    if add:
      if re.match(DICTIONARY_WORD, add):
        additions += [add]
      else:
        print("Cannot add %s to the dictionary: it may only contain letter and apostrophes" % add)

  if len(errors) != len(replacements):
    print("Internal error %d errors with %d replacements" % (len(errors), len(replacements)))
    sys.exit(2)

  # Perform replacements on the line.
  line = lines[line_offset]
  for idx in range(len(replacements) - 1, -1, -1):
    word, offset, _ = errors[idx]
    replacement = replacements[idx]
    if word == replacement:
      continue

    line = line[:offset] + replacement + line[offset + len(word):]
  lines[line_offset] = line

  # Update the dictionary.
  checker.add_words(additions)


def check_file(checker, file, lines, error_handler):
  in_comment = False
  num_comments = 0
  num_errors = 0
  for line_idx, line in enumerate(lines):
    errors = []
    last = 0
    if in_comment:
      mc_end = MULTI_COMMENT_END.search(line)
      if mc_end is None:
        # Full line is within a multi-line comment.
        errors += check_comment(checker, 0, line)
        num_comments += 1
      else:
        # Start of line is the end of a multi-line comment.
        errors += check_comment(checker, 0, mc_end.group(1))
        num_comments += 1
        last = mc_end.end()
        in_comment = False

    if not in_comment:
      for inline in INLINE_COMMENT.finditer(line, last):
        # Single-line comment.
        m = inline.lastindex  # 1 or 2 depending on group matched.
        errors += check_comment(checker, inline.start(m), inline.group(m))
        num_comments += 1
        last = inline.end(m)
      if last < len(line):
        mc_start = MULTI_COMMENT_START.search(line, last)
        if mc_start is not None:
          # New multi-lie comment starts at end of line.
          errors += check_comment(checker, mc_start.start(1), mc_start.group(1))
          num_comments += 1
          in_comment = True

    if errors:
      num_errors += len(errors)
      error_handler(file, line_idx, lines, errors)

  return (num_comments, num_errors)


def execute(files, dictionary_file, fix):
  checker = SpellChecker(dictionary_file)
  checker.start()

  handler = print_error
  if fix:
    handler = partial(fix_error, checker)

  total_files = 0
  total_comments = 0
  total_errors = 0
  for path in files:
    with open(path, 'r') as f:
      lines = f.readlines()
      total_files += 1
      (num_comments, num_errors) = check_file(checker, path, lines, handler)
      total_comments += num_comments
      total_errors += num_errors

    if fix and num_errors > 0:
      with open(path, 'w') as f:
        f.writelines(lines)

  checker.stop()

  print("Checked %d file(s) and %d comment(s), found %d error(s)." %
        (total_files, total_comments, total_errors))

  return total_errors == 0


if __name__ == "__main__":
  # Force UTF-8 across all open and popen calls. Fallback to 'C' as the
  # language to handle hosts where en_US is not recognized (e.g. CI).
  try:
    locale.setlocale(locale.LC_ALL, 'en_US.UTF-8')
  except:
    locale.setlocale(locale.LC_ALL, 'C.UTF-8')

  default_dictionary = os.path.join(TOOLS_DIR, 'spelling_dictionary.txt')

  parser = argparse.ArgumentParser(description="Check comment spelling.")
  parser.add_argument('operation_type',
                      type=str,
                      choices=['check', 'fix'],
                      help="specify if the run should 'check' or 'fix' spelling.")
  parser.add_argument('target_paths',
                      type=str,
                      nargs="*",
                      help="specify the files for the script to process.")
  parser.add_argument('-d', '--debug', action='store_true', help="Debug spell checker subprocess.")
  parser.add_argument('--mark',
                      action='store_true',
                      help="Emits extra output to mark misspelled words.")
  parser.add_argument('--dictionary',
                      type=str,
                      default=default_dictionary,
                      help="specify a location for Envoy-specific dictionary words")
  parser.add_argument('--color',
                      type=str,
                      choices=['on', 'off', 'auto'],
                      default="auto",
                      help="Controls colorized output. Auto limits color to TTY devices.")
  args = parser.parse_args()

  COLOR = args.color == "on" or (args.color == "auto" and sys.stdout.isatty())
  DEBUG = args.debug
  MARK = args.mark

  paths = args.target_paths
  if not paths:
    paths = ['./api', './include', './source', './test']

  exts = ['.cc', '.h', '.proto']
  target_paths = []
  for p in paths:
    if os.path.isdir(p):
      for root, _, files in os.walk(p):
        target_paths += [os.path.join(root, f) for f in files if os.path.splitext(f)[1] in exts]
    if os.path.isfile(p) and os.path.splitext(p)[1] in exts:
      target_paths += [p]

  rv = execute(target_paths, args.dictionary, args.operation_type == 'fix')

  if args.operation_type == 'check':
    if not rv:
      print("ERROR: spell check failed. Run 'tools/check_spelling_pedantic.py fix and/or add new "
            "words to tools/spelling_dictionary.txt'")
      sys.exit(1)

    print("PASS")
