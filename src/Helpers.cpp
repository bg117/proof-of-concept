#include "fatfs/Helpers.hpp"

#include "utilities/String.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>

std::string
Fatfs::Helpers::Path::ConvertLongPathToFatPath(const std::string_view name)
{
    std::string r = Utilities::String::TrimString(name);

    if (r == ".")
        return ".          "; // trivial case #1 (dot)

    if (r == "..")
        return "..         "; // trivial case #2 (dotdot)

    std::string result;
    result.reserve(11);

    auto       it  = r.begin();
    const auto end = r.end();

    // Copy the first 8 characters or up to the first dot
    for (int i = 0; i < 8 && it != end && *it != '.'; ++i, ++it)
        result += *it;

    // pad with spaces
    for (std::size_t i = result.size(); i < 8; ++i)
        result += ' ';

    // skip dot
    if (it != end && *it == '.')
        ++it;

    // copy the extension
    for (int i = 0; i < 3 && it != end; ++i, ++it)
        result += *it;

    // pad with spaces
    for (std::size_t i = result.size(); i < 11; ++i)
        result += ' ';

    // uppercase
    std::transform(result.begin(), result.end(), result.begin(), toupper);

    return result;
}

std::string
Fatfs::Helpers::Path::ConvertFatPathToLongPath(const std::string_view name)
{
    std::string r = Utilities::String::TrimString(name);

    if (r == ".          ")
        return ".";

    if (r == "..         ")
        return "..";

    // if r is not 11 characters long, resize and optionally pad with spaces
    if (r.size() != 11)
        r.resize(11, ' ');

    std::string result;
    result.reserve(11);

    auto       it  = r.begin();
    const auto end = r.end();

    // copy the first 8 characters
    for (int i = 0; i < 8 && it != end; ++i, ++it)
        result += *it;

    // remove trailing spaces
    result.erase(std::find_if(result.rbegin(),
                              result.rend(),
                              [](const char c)
                              {
                                  return c != ' ';
                              })
                     .base(),
                 result.end());

    // if there is an extension, add a dot
    if (*it != ' ' && *(it + 1) != ' ' && *(it + 2) != ' ')
        result += '.';

    // copy the extension
    for (int i = 0; i < 3 && it != end; ++i, ++it)
        result += *it;

    // remove trailing spaces
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

std::vector<std::string>
Fatfs::Helpers::Path::SplitLongPathToFatComponents(const std::string_view path)
{
    std::string              tmp;
    std::stringstream        ss{path.data()};
    std::vector<std::string> pathComponents{};

    // split on backslash
    while (std::getline(ss, tmp, '\\'))
    {
        pathComponents.emplace_back(
            ConvertLongPathToFatPath(Utilities::String::TrimString(tmp)));
    }

    // erase empty components
    pathComponents.erase(
        std::remove_if(pathComponents.begin(),
                       pathComponents.end(),
                       [](const std::string_view x)
                       {
                           return Utilities::String::TrimString(x).empty();
                       }),
        pathComponents.end());

    // resize if there are empty stuff
    pathComponents.shrink_to_fit();

    return pathComponents;
}

std::chrono::system_clock::time_point
Fatfs::Helpers::Time::ConvertFatTimeToUnixTime(Structures::TimeFormat time,
                                               Structures::DateFormat date)
{
    std::tm timeTm{};
    timeTm.tm_sec =
        time.Second *
        2; // multiply by 2 because FAT time is in 2 second intervals
    timeTm.tm_min  = time.Minute;
    timeTm.tm_hour = time.Hour;
    timeTm.tm_mday = date.Day;
    timeTm.tm_mon  = date.Month -
                    1; // subtract 1 because FAT month is 1-12 and m_mon is 0-11
    timeTm.tm_year = date.Year + 80; // add 80 because FAT year starts from 1980
    // and tm_year is 1900-...

    const std::time_t timeT = std::mktime(&timeTm);

    // return time_point
    return std::chrono::system_clock::from_time_t(timeT);
}

std::tuple<Fatfs::Structures::TimeFormat, Fatfs::Structures::DateFormat>
Fatfs::Helpers::Time::ConvertUnixTimeToFatTime(
    const std::chrono::system_clock::time_point &timePoint)
{
    const std::time_t timeT  = std::chrono::system_clock::to_time_t(timePoint);
    const std::tm    *timeTm = std::localtime(&timeT);

    Structures::TimeFormat time{};
    Structures::DateFormat date{};

    date.Day   = timeTm->tm_mday;
    date.Month = timeTm->tm_mon + 1;
    date.Year =
        timeTm->tm_year - 80; // 1980 is the base year; (struct tm*)->tm_year is
    // the number of years since 1900

    time.Hour   = timeTm->tm_hour;
    time.Minute = timeTm->tm_min;
    time.Second = timeTm->tm_sec / 2; // 2 second resolution

    // tuple of [time, date], can be deconstructed
    return std::make_tuple(time, date);
}