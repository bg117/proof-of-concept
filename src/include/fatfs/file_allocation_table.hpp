#pragma once

#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fatfs
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

    // delete copy and move constructors and assignment operators
    file_allocation_table(const file_allocation_table &) = delete;
    file_allocation_table &operator=(const file_allocation_table &) = delete;

    file_allocation_table(file_allocation_table &&) = delete;
    file_allocation_table &operator=(file_allocation_table &&) = delete;

    [[nodiscard]] std::vector<file_info> read_directory(std::string_view path) const;
    [[nodiscard]] std::vector<std::byte> read_file(std::string_view path) const;

    void create_file(std::string_view path, const std::vector<std::byte> &data) const;
    
    void create_directory(std::string_view path) const;

    [[nodiscard]] file_system_version version() const;
  private:
    // PImpl idiom
    class impl;
    std::unique_ptr<impl> m_impl;
};
} // namespace fatfs
