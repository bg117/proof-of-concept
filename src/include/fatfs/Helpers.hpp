#ifndef FATFS_HELPERS_HPP
#define FATFS_HELPERS_HPP

#include "fatfs/Structures.hpp"

#include <chrono>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace Fatfs
{
namespace Helpers
{

namespace Path
{
std::string ConvertLongPathToFatPath(std::string_view name);
std::string ConvertFatPathToLongPath(std::string_view name);

std::vector<std::string> SplitLongPathToFatComponents(std::string_view path);
} // namespace Path

namespace Time
{
std::chrono::system_clock::time_point
ConvertFatTimeToUnixTime(Structures::TimeFormat time,
                         Structures::DateFormat date);

std::tuple<Fatfs::Structures::TimeFormat, Fatfs::Structures::DateFormat>
ConvertUnixTimeToFatTime(
    const std::chrono::system_clock::time_point &timePoint);
} // namespace Time

} // namespace Helpers
} // namespace Fatfs

#endif // FATFS_HELPERS_HPP
