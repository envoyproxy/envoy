#pragma once

#include <list>
#include <string>

#include "envoy/json/json_object.h"

namespace Json {

class Factory {
public:
  /*
   * Constructs a Json Object from a File.
   */
  static ObjectPtr loadFromFile(const std::string& file_path);
  // static ObjectPtr loadFromFileTwo(const std::string& file_path);

  /*
   * Constructs a Json Object from a String.
   */
  static ObjectPtr loadFromString(const std::string& json);

  static const std::string listAsJsonString(const std::list<std::string>& items);
};

} // Json
