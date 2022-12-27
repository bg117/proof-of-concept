#pragma once

#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Fatfs
{
enum class FileSystemVersion
{
    Fat12,
    Fat16,
    Fat32
};

struct FileInfo
{
    std::string Name;

    std::time_t CreationTimestamp;
    std::time_t LastModificationTimestamp;
    std::time_t LastAccessDate;

    std::size_t Size; // 0 if directory

    bool IsDirectory;
};

class FileAllocationTable
{
  public:
    explicit FileAllocationTable(std::string_view path);
    ~FileAllocationTable();

    // delete copy and move constructors and assignment operators
    FileAllocationTable(const FileAllocationTable &) = delete;
    FileAllocationTable &operator=(const FileAllocationTable &) = delete;

    FileAllocationTable(FileAllocationTable &&) = delete;
    FileAllocationTable &operator=(FileAllocationTable &&) = delete;

    [[nodiscard]] std::vector<FileInfo>
    ReadDirectory(std::string_view path) const;
    [[nodiscard]] std::vector<std::byte> ReadFile(std::string_view path) const;

    void CreateFile(std::string_view path, const std::vector<std::byte> &data) const;
    void CreateDirectory(std::string_view path) const;

    void DeleteEntry(std::string_view path) const;
    void EraseEntry(std::string_view path) const;

    [[nodiscard]] FileSystemVersion Version() const;
  private:
    // PImpl idiom
    class Implementation;
    std::unique_ptr<Implementation> impl_;
};
} // namespace fatfs
