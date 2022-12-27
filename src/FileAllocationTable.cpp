#include "fatfs/FileAllocationTable.hpp"
#include "fatfs/FileAllocationTable.impl.hpp"

Fatfs::FileAllocationTable::FileAllocationTable(std::string_view path)
{
    impl_ = std::make_unique<Implementation>(path);
}

Fatfs::FileAllocationTable::~FileAllocationTable() = default;

std::vector<Fatfs::FileInfo>
Fatfs::FileAllocationTable::ReadDirectory(std::string_view path) const
{
    return impl_->ReadDirectory(path);
}

std::vector<std::byte>
Fatfs::FileAllocationTable::ReadFile(std::string_view path) const
{
    return impl_->ReadFile(path);
}

void Fatfs::FileAllocationTable::CreateFile(
    std::string_view              path,
    const std::vector<std::byte> &data) const
{
    impl_->CreateFile(path, data);
}

void Fatfs::FileAllocationTable::CreateDirectory(std::string_view path) const
{
    impl_->CreateDirectory(path);
}

void Fatfs::FileAllocationTable::DeleteEntry(std::string_view path) const
{
    impl_->DeleteEntry(path);
}

void Fatfs::FileAllocationTable::EraseEntry(std::string_view path) const
{
    impl_->EraseEntry(path);
}

Fatfs::FileSystemVersion Fatfs::FileAllocationTable::Version() const
{
    return impl_->Version();
}
