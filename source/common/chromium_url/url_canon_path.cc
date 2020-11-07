// Envoy snapshot of Chromium URL path normalization, see README.md.
// NOLINT(namespace-envoy)

// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include "common/chromium_url/url_canon.h"
#include "common/chromium_url/url_canon_internal.h"
#include "common/chromium_url/url_parse_internal.h"

namespace chromium_url {

namespace {

enum CharacterFlags {
  // Pass through unchanged, whether escaped or unescaped. This doesn't
  // actually set anything so you can't OR it to check, it's just to make the
  // table below more clear when neither ESCAPE or UNESCAPE is set.
  PASS = 0,

  // This character requires special handling in DoPartialPath. Doing this test
  // first allows us to filter out the common cases of regular characters that
  // can be directly copied.
  SPECIAL = 1,

  // This character must be escaped in the canonical output. Note that all
  // escaped chars also have the "special" bit set so that the code that looks
  // for this is triggered. Not valid with PASS or ESCAPE
  ESCAPE_BIT = 2,
  ESCAPE = ESCAPE_BIT | SPECIAL,

  // This character must be unescaped in canonical output. Not valid with
  // ESCAPE or PASS. We DON'T set the SPECIAL flag since if we encounter these
  // characters unescaped, they should just be copied.
  UNESCAPE = 4,

  // This character is disallowed in URLs. Note that the "special" bit is also
  // set to trigger handling.
  INVALID_BIT = 8,
  INVALID = INVALID_BIT | SPECIAL,
};

// This table contains one of the above flag values. Note some flags are more
// than one bits because they also turn on the "special" flag. Special is the
// only flag that may be combined with others.
//
// This table is designed to match exactly what IE does with the characters.
//
// Dot is even more special, and the escaped version is handled specially by
// IsDot. Therefore, we don't need the "escape" flag, and even the "unescape"
// bit is never handled (we just need the "special") bit.
const unsigned char kPathCharLookup[0x100] = {
    //   NULL     control chars...
    INVALID, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    //   control chars...
    ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    //   ' '      !        "        #        $        %        &        '        (        )        *
    //   +        ,        -        . /
    ESCAPE, PASS, ESCAPE, ESCAPE, PASS, ESCAPE, PASS, PASS, PASS, PASS, PASS, PASS, PASS, UNESCAPE,
    SPECIAL, PASS,
    //   0        1        2        3        4        5        6        7        8        9        :
    //   ;        <        =        >        ?
    UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE,
    UNESCAPE, PASS, PASS, ESCAPE, PASS, ESCAPE, ESCAPE,
    //   @        A        B        C        D        E        F        G        H        I        J
    //   K        L        M        N        O
    PASS, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE,
    UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE,
    //   P        Q        R        S        T        U        V        W        X        Y        Z
    //   [        \        ]        ^        _
    UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE,
    UNESCAPE, UNESCAPE, PASS, ESCAPE, PASS, ESCAPE, UNESCAPE,
    //   `        a        b        c        d        e        f        g        h        i        j
    //   k        l        m        n        o
    ESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE,
    UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE,
    //   p        q        r        s        t        u        v        w        x        y        z
    //   {        |        }        ~        <NBSP>
    UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE, UNESCAPE,
    UNESCAPE, UNESCAPE, ESCAPE, ESCAPE, ESCAPE, UNESCAPE, ESCAPE,
    //   ...all the high-bit characters are escaped
    ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE,
    ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE, ESCAPE};

enum DotDisposition {
  // The given dot is just part of a filename and is not special.
  NOT_A_DIRECTORY,

  // The given dot is the current directory.
  DIRECTORY_CUR,

  // The given dot is the first of a double dot that should take us up one.
  DIRECTORY_UP
};

// When the path resolver finds a dot, this function is called with the
// character following that dot to see what it is. The return value
// indicates what type this dot is (see above). This code handles the case
// where the dot is at the end of the input.
//
// |*consumed_len| will contain the number of characters in the input that
// express what we found.
//
// If the input is "../foo", |after_dot| = 1, |end| = 6, and
// at the end, |*consumed_len| = 2 for the "./" this function consumed. The
// original dot length should be handled by the caller.
template <typename CHAR>
DotDisposition ClassifyAfterDot(const CHAR* spec, int after_dot, int end, int* consumed_len) {
  if (after_dot == end) {
    // Single dot at the end.
    *consumed_len = 0;
    return DIRECTORY_CUR;
  }
  if (IsURLSlash(spec[after_dot])) {
    // Single dot followed by a slash.
    *consumed_len = 1; // Consume the slash
    return DIRECTORY_CUR;
  }

  int second_dot_len = IsDot(spec, after_dot, end);
  if (second_dot_len) {
    int after_second_dot = after_dot + second_dot_len;
    if (after_second_dot == end) {
      // Double dot at the end.
      *consumed_len = second_dot_len;
      return DIRECTORY_UP;
    }
    if (IsURLSlash(spec[after_second_dot])) {
      // Double dot followed by a slash.
      *consumed_len = second_dot_len + 1;
      return DIRECTORY_UP;
    }
  }

  // The dots are followed by something else, not a directory.
  *consumed_len = 0;
  return NOT_A_DIRECTORY;
}

// Rewinds the output to the previous slash. It is assumed that the output
// ends with a slash and this doesn't count (we call this when we are
// appending directory paths, so the previous path component has and ending
// slash).
//
// This will stop at the first slash (assumed to be at position
// |path_begin_in_output| and not go any higher than that. Some web pages
// do ".." too many times, so we need to handle that brokenness.
//
// It searches for a literal slash rather than including a backslash as well
// because it is run only on the canonical output.
//
// The output is guaranteed to end in a slash when this function completes.
void BackUpToPreviousSlash(int path_begin_in_output, CanonOutput* output) {
  DCHECK(output->length() > 0);

  int i = output->length() - 1;
  DCHECK(output->at(i) == '/');
  if (i == path_begin_in_output)
    return; // We're at the first slash, nothing to do.

  // Now back up (skipping the trailing slash) until we find another slash.
  i--;
  while (output->at(i) != '/' && i > path_begin_in_output)
    i--;

  // Now shrink the output to just include that last slash we found.
  output->set_length(i + 1);
}

// Looks for problematic nested escape sequences and escapes the output as
// needed to ensure they can't be misinterpreted.
//
// Our concern is that in input escape sequence that's invalid because it
// contains nested escape sequences might look valid once those are unescaped.
// For example, "%%300" is not a valid escape sequence, but after unescaping the
// inner "%30" this becomes "%00" which is valid. Leaving this in the output
// string can result in callers re-canonicalizing the string and unescaping this
// sequence, thus resulting in something fundamentally different than the
// original input here. This can cause a variety of problems.
//
// This function is called after we've just unescaped a sequence that's within
// two output characters of a previous '%' that we know didn't begin a valid
// escape sequence in the input string. We look for whether the output is going
// to turn into a valid escape sequence, and if so, convert the initial '%' into
// an escaped "%25" so the output can't be misinterpreted.
//
// |spec| is the input string we're canonicalizing.
// |next_input_index| is the index of the next unprocessed character in |spec|.
// |input_len| is the length of |spec|.
// |last_invalid_percent_index| is the index in |output| of a previously-seen
// '%' character. The caller knows this '%' character isn't followed by a valid
// escape sequence in the input string.
// |output| is the canonicalized output thus far. The caller guarantees this
// ends with a '%' followed by one or two characters, and the '%' is the one
// pointed to by |last_invalid_percent_index|. The last character in the string
// was just unescaped.
template <typename CHAR>
void CheckForNestedEscapes(const CHAR* spec, int next_input_index, int input_len,
                           int last_invalid_percent_index, CanonOutput* output) {
  const int length = output->length();
  const char last_unescaped_char = output->at(length - 1);

  // If |output| currently looks like "%c", we need to try appending the next
  // input character to see if this will result in a problematic escape
  // sequence. Note that this won't trigger on the first nested escape of a
  // two-escape sequence like "%%30%30" -- we'll allow the conversion to
  // "%0%30" -- but the second nested escape will be caught by this function
  // when it's called again in that case.
  const bool append_next_char = last_invalid_percent_index == length - 2;
  if (append_next_char) {
    // If the input doesn't contain a 7-bit character next, this case won't be a
    // problem.
    if ((next_input_index == input_len) || (spec[next_input_index] >= 0x80))
      return;
    output->push_back(static_cast<char>(spec[next_input_index]));
  }

  // Now output ends like "%cc". Try to unescape this.
  int begin = last_invalid_percent_index;
  unsigned char temp;
  if (DecodeEscaped(output->data(), &begin, output->length(), &temp)) {
    // New escape sequence found. Overwrite the characters following the '%'
    // with "25", and push_back() the one or two characters that were following
    // the '%' when we were called.
    if (!append_next_char)
      output->push_back(output->at(last_invalid_percent_index + 1));
    output->set(last_invalid_percent_index + 1, '2');
    output->set(last_invalid_percent_index + 2, '5');
    output->push_back(last_unescaped_char);
  } else if (append_next_char) {
    // Not a valid escape sequence, but we still need to undo appending the next
    // source character so the caller can process it normally.
    output->set_length(length);
  }
}

// Appends the given path to the output. It assumes that if the input path
// starts with a slash, it should be copied to the output. If no path has
// already been appended to the output (the case when not resolving
// relative URLs), the path should begin with a slash.
//
// If there are already path components (this mode is used when appending
// relative paths for resolving), it assumes that the output already has
// a trailing slash and that if the input begins with a slash, it should be
// copied to the output.
//
// We do not collapse multiple slashes in a row to a single slash. It seems
// no web browsers do this, and we don't want incompatibilities, even though
// it would be correct for most systems.
template <typename CHAR, typename UCHAR>
bool DoPartialPath(const CHAR* spec, const Component& path, int path_begin_in_output,
                   CanonOutput* output) {
  int end = path.end();

  // We use this variable to minimize the amount of work done when unescaping --
  // we'll only call CheckForNestedEscapes() when this points at one of the last
  // couple of characters in |output|.
  int last_invalid_percent_index = INT_MIN;

  bool success = true;
  for (int i = path.begin; i < end; i++) {
    UCHAR uch = static_cast<UCHAR>(spec[i]);
    // Chromium UTF8 logic is unneeded, as the missing templated result
    // refers only to char const* (single-byte) characters at this time.
    // This only trips up MSVC, since linux gcc seems to optimize it away.
    // Indention is to avoid gratuitous diffs to origin source
    {
      unsigned char out_ch = static_cast<unsigned char>(uch);
      unsigned char flags = kPathCharLookup[out_ch];
      if (flags & SPECIAL) {
        // Needs special handling of some sort.
        int dotlen;
        if ((dotlen = IsDot(spec, i, end)) > 0) {
          // See if this dot was preceded by a slash in the output. We
          // assume that when canonicalizing paths, they will always
          // start with a slash and not a dot, so we don't have to
          // bounds check the output.
          //
          // Note that we check this in the case of dots so we don't have to
          // special case slashes. Since slashes are much more common than
          // dots, this actually increases performance measurably (though
          // slightly).
          DCHECK(output->length() > path_begin_in_output);
          if (output->length() > path_begin_in_output && output->at(output->length() - 1) == '/') {
            // Slash followed by a dot, check to see if this is means relative
            int consumed_len;
            switch (ClassifyAfterDot<CHAR>(spec, i + dotlen, end, &consumed_len)) {
            case NOT_A_DIRECTORY:
              // Copy the dot to the output, it means nothing special.
              output->push_back('.');
              i += dotlen - 1;
              break;
            case DIRECTORY_CUR: // Current directory, just skip the input.
              i += dotlen + consumed_len - 1;
              break;
            case DIRECTORY_UP:
              BackUpToPreviousSlash(path_begin_in_output, output);
              i += dotlen + consumed_len - 1;
              break;
            }
          } else {
            // This dot is not preceded by a slash, it is just part of some
            // file name.
            output->push_back('.');
            i += dotlen - 1;
          }

        } else if (out_ch == '\\') {
          // Convert backslashes to forward slashes
          output->push_back('/');

        } else if (out_ch == '%') {
          // Handle escape sequences.
          unsigned char unescaped_value;
          if (DecodeEscaped(spec, &i, end, &unescaped_value)) {
            // Valid escape sequence, see if we keep, reject, or unescape it.
            // Note that at this point DecodeEscape() will have advanced |i| to
            // the last character of the escape sequence.
            char unescaped_flags = kPathCharLookup[unescaped_value];

            if (unescaped_flags & UNESCAPE) {
              // This escaped value shouldn't be escaped. Try to copy it.
              output->push_back(unescaped_value);
              // If we just unescaped a value within 2 output characters of the
              // '%' from a previously-detected invalid escape sequence, we
              // might have an input string with problematic nested escape
              // sequences; detect and fix them.
              if (last_invalid_percent_index >= (output->length() - 3)) {
                CheckForNestedEscapes(spec, i + 1, end, last_invalid_percent_index, output);
              }
            } else {
              // Either this is an invalid escaped character, or it's a valid
              // escaped character we should keep escaped. In the first case we
              // should just copy it exactly and remember the error. In the
              // second we also copy exactly in case the server is sensitive to
              // changing the case of any hex letters.
              output->push_back('%');
              output->push_back(static_cast<char>(spec[i - 1]));
              output->push_back(static_cast<char>(spec[i]));
              if (unescaped_flags & INVALID_BIT)
                success = false;
            }
          } else {
            // Invalid escape sequence. IE7+ rejects any URLs with such
            // sequences, while other browsers pass them through unchanged. We
            // use the permissive behavior.
            // TODO(brettw): Consider testing IE's strict behavior, which would
            // allow removing the code to handle nested escapes above.
            last_invalid_percent_index = output->length();
            output->push_back('%');
          }

        } else if (flags & INVALID_BIT) {
          // For NULLs, etc. fail.
          AppendEscapedChar(out_ch, output);
          success = false;

        } else if (flags & ESCAPE_BIT) {
          // This character should be escaped.
          AppendEscapedChar(out_ch, output);
        }
      } else {
        // Nothing special about this character, just append it.
        output->push_back(out_ch);
      }
    }
  }
  return success;
}

template <typename CHAR, typename UCHAR>
bool DoPath(const CHAR* spec, const Component& path, CanonOutput* output, Component* out_path) {
  bool success = true;
  out_path->begin = output->length();
  if (path.len > 0) {
    // Write out an initial slash if the input has none. If we just parse a URL
    // and then canonicalize it, it will of course have a slash already. This
    // check is for the replacement and relative URL resolving cases of file
    // URLs.
    if (!IsURLSlash(spec[path.begin]))
      output->push_back('/');

    success = DoPartialPath<CHAR, UCHAR>(spec, path, out_path->begin, output);
  } else {
    // No input, canonical path is a slash.
    output->push_back('/');
  }
  out_path->len = output->length() - out_path->begin;
  return success;
}

} // namespace

bool CanonicalizePath(const char* spec, const Component& path, CanonOutput* output,
                      Component* out_path) {
  return DoPath<char, unsigned char>(spec, path, output, out_path);
}

} // namespace chromium_url
