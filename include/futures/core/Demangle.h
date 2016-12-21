/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>
#include <typeinfo>

#ifdef __GNUG__
#include <cstdlib>
#include <cstring>
#include <memory>
#include <cxxabi.h>
#endif

namespace folly {

/**
 * Return the demangled (prettyfied) version of a C++ type.
 *
 * This function tries to produce a human-readable type, but the type name will
 * be returned unchanged in case of error or if demangling isn't supported on
 * your system.
 *
 * Use for debugging -- do not rely on demangle() returning anything useful.
 *
 * This function may allocate memory (and therefore throw std::bad_alloc).
 */
inline std::string demangle(const char* name) {
#ifdef __GNUG__
  int status;
  size_t len = 0;
  // malloc() memory for the demangled type name
  char* demangled = abi::__cxa_demangle(name, nullptr, &len, &status);
  if (status != 0) {
    return name;
  }
  // len is the length of the buffer (including NUL terminator and maybe
  // other junk)
  std::string s(demangled, strlen(demangled));
  free(demangled);
  return s;
#else
  return name;
#endif
}

inline std::string demangle(const std::type_info& type) {
  return demangle(type.name());
}


}
