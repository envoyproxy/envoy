#pragma once

#include <string>

#include "envoy/json/json_object.h"

namespace Json {

class Factory {
public:
  /*
   * Constructs a Json Object from a File.
   */
  static ObjectPtr LoadFromFile(const std::string& file_path);

  /*
   * Constructs a Json Object from a String.
   */
  static ObjectPtr LoadFromString(const std::string& json);
};

} // Json
