# Fix rapidjson GCC 15: GenericStringRef::operator= assigns const member 'length'
file(READ "${src}" content)
string(REPLACE
  "GenericStringRef& operator=(const GenericStringRef& rhs) { s = rhs.s; length = rhs.length; }"
  "GenericStringRef& operator=(const GenericStringRef& rhs) { s = rhs.s; }"
  content "${content}")
file(WRITE "${src}" "${content}")
