#include "utilities/String.hpp"

std::string Utilities::String::TrimString(std::string_view str)
{
    std::string result{str};

    // remove all that isn't a space BUT only from the start and end
    result.erase(std::find_if(result.rbegin(),
                              result.rend(),
                              [](const char c)
                              {
                                  return c != ' ';
                              })
                     .base(),
                 result.end());

    return result;
}
