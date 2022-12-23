#pragma once

#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace poc
{
enum class file_system_version
{
    fat12,
    fat16,
    fat32
};

struct file_info
{
    std::string name;

    std::time_t creation_timestamp;
    std::time_t last_modification_timestamp;
    std::time_t last_access_date;

    std::size_t size; // 0 if directory

    bool is_directory;
};

class file_allocation_table
{

  public:
    explicit file_allocation_table(std::string_view path);
    ~file_allocation_table();

    std::vector<file_info> read_directory(std::string_view path);
    std::vector<std::byte> read_file(std::string_view path);

    void write_file(std::string_view path, const std::vector<std::byte> &data);

    file_system_version version() const;
  private:
    // PImpl idiom
    struct impl;
    std::unique_ptr<impl> m_impl;
};
} // namespace poc
