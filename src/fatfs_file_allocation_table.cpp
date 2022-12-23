#include "fatfs/file_allocation_table.hpp"

#define FATFS_ALLOW_PRIV_IMPL
#include "fatfs/priv/file_allocation_table__impl.hpp"

fatfs::file_allocation_table::file_allocation_table(std::string_view path)
{
    m_impl = std::make_unique<impl>(path);
}

fatfs::file_allocation_table::~file_allocation_table() = default;

std::vector<fatfs::file_info>
fatfs::file_allocation_table::read_directory(const std::string_view path) const
{
    return m_impl->read_directory(path);
}

std::vector<std::byte>
fatfs::file_allocation_table::read_file(const std::string_view path) const
{
    return m_impl->read_file(path);
}

void fatfs::file_allocation_table::write_file(const std::string_view path,
                                              const std::vector<std::byte> &
                                              data)
const
{
    m_impl->write_file(path, data);
}

fatfs::file_system_version fatfs::file_allocation_table::version() const
{
    return m_impl->version();
}
