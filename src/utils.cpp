#include "utils.hpp"

const char* modloader::copyStrC(std::string_view const s) {
    char* newStr = static_cast<char*>(malloc(s.size() + 1));
    std::copy(s.begin(), s.end(), newStr);
    return newStr;
}
