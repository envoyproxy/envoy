#include "extensions/tracers/xray/util.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <regex>
#include <vector>

#include "common/common/hex.h"
#include "common/common/utility.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {
void Util::mergeJsons(std::string& target, const std::string& source,
                      const std::string& field_name) {
  rapidjson::Document target_doc, source_doc;
  target_doc.Parse(target.c_str());
  source_doc.Parse(source.c_str());

  target_doc.AddMember(rapidjson::StringRef(field_name.c_str()), source_doc,
                       target_doc.GetAllocator());

  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> w(sb);
  target_doc.Accept(w);
  target = sb.GetString();
}

void Util::addArrayToJson(std::string& target, const std::vector<std::string>& json_array,
                          const std::string& field_name) {
  std::string stringified_json_array = "[";

  if (json_array.size() > 0) {
    stringified_json_array += json_array[0];
    for (auto it = json_array.begin() + 1; it != json_array.end(); it++) {
      stringified_json_array += ",";
      stringified_json_array += *it;
    }
  }
  stringified_json_array += "]";

  mergeJsons(target, stringified_json_array, field_name);
}

uint64_t Util::generateRandom64(TimeSource& time_source) {
  uint64_t seed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      time_source.systemTime().time_since_epoch())
                      .count();
  std::mt19937_64 rand_64(seed);
  return rand_64();
}

double Util::generateRandomDouble(TimeSource& time_source) {
  uint64_t seed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      time_source.systemTime().time_since_epoch())
                      .count();
  std::mt19937 gen(seed);
  std::uniform_real_distribution<double> unif(0, 1);
  double random_double = unif(gen);
  return random_double;
}

bool Util::wildcardMatch(std::string& pattern, std::string& text) {
  if (pattern.empty()) {
    return text.empty();
  }

  size_t pattern_length = pattern.size();
  size_t text_length = text.size();

  // Check the special case of a single * pattern, as it's common.
  if (isWildcardGlob(pattern)) {
    return true;
  }

  std::transform(pattern.begin(), pattern.end(), pattern.begin(), ::tolower);
  std::transform(text.begin(), text.end(), text.begin(), ::tolower);

  // Infix globs are relatively rare, and the below search is expensive especially when
  // Balsa is used a lot. Check for infix globs and, in their absence, do the simple thing
  size_t index_of_glob = pattern.find("*");
  if (index_of_glob == std::string::npos || index_of_glob == pattern_length - 1) {
    return simpleWildcardMatch(pattern, text);
  }

  /*
   * The res[i] is used to record if there is a match
   * between the first i chars in text and the first j chars in pattern.
   * So will return res[text_length+1] in the end
   * Loop from the beginning of the pattern
   * case not '*': if text[i]==pattern[j] or pattern[j] is '?', and res[i] is true,
   *   set res[i+1] to true, otherwise false
   * case '*': since '*' can match any globing, as long as there is a true in res before i
   *   all the res[i+1], res[i+2],...,res[text_length] could be true
   */

  std::vector<size_t> res(text_length + 1);
  res[0] = true;
  for (size_t j = 0; j < pattern_length; j++) {
    char p = pattern.at(j);
    if (p != '*') {
      for (size_t i = text_length - 1; i != std::string::npos; i--) {
        char t = text.at(i);
        res[i + 1] = res[i] && (p == '?' || (p == t));
      }
    } else {
      size_t i = 0;
      while (i <= text_length && !res[i]) {
        i++;
      }
      for (; i <= text_length; i++) {
        res[i] = true;
      }
    }
    res[0] = res[0] && p == '*';
  }
  return res[text_length];
}

bool Util::simpleWildcardMatch(const std::string& pattern, const std::string& text) {
  size_t j = 0;
  size_t pattern_length = pattern.size();
  size_t text_length = text.size();
  for (size_t i = 0; i < pattern_length; i++) {
    char p = pattern.at(i);
    if (p == '*') {
      // Presumption for this method is that globs only occur at end
      return true;
    } else if (p == '?') {
      if (j == text_length) {
        return false; // No character to match
      }
      j++;
    } else {
      if (j >= text_length) {
        return false;
      }
      char t = text.at(j);
      if (p != t) {
        return false;
      }
      j++;
    }
  }
  // Ate up all the pattern and didn't end at a glob, so a match will have consumed all
  // the text
  return j == text_length;
}

bool Util::isWildcardGlob(const std::string& pattern) {
  return pattern.size() == 1 && pattern.at(0) == '*';
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
