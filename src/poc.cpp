#include "poc.hpp"

#include <algorithm>
#include <bits/stdint-uintn.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iterator>
#include <sstream>
#include <unordered_set>

namespace
{
std::string trim_string(const std::string &str);
}

poc::file_allocation_table::file_allocation_table(std::string_view path)
    : m_bpb()
{
    std::fstream fs;
    fs.open(path.data(), std::ios::binary | std::ios::in | std::ios::out);

    m_fs = std::move(fs);

    if (!m_fs.is_open())
        throw std::runtime_error{"failed to open file " + std::string{path}};

    // copy BPB to struct
    m_fs.seekg(0);
    m_fs.read(reinterpret_cast<char *>(&m_bpb), sizeof m_bpb);
}

std::vector<poc::directory_entry>
poc::file_allocation_table::read_root_directory()
{
    const version fsver           = file_system_version();
    std::size_t   sectors_per_fat = fsver == version::fat32
                                        ? m_bpb.offset_36.fat32.sectors_per_fat_32
                                        : m_bpb.sectors_per_fat_16;

    const std::size_t start_of_root_directory =
        m_bpb.reserved_sectors + m_bpb.number_of_fats * sectors_per_fat;

    directory_type root_directory{};

    if (file_system_version() != version::fat32)
    {
        // root directory in FAT12 and FAT16 has a fixed size and is located at
        // a fixed offset (directly after the FAT table)
        root_directory =
            directory_type(sizeof(directory_entry) * m_bpb.root_dir_entries);

        m_fs.seekg(start_of_root_directory * m_bpb.bytes_per_sector);
        m_fs.read(reinterpret_cast<char *>(root_directory.data()),
                  sizeof(directory_entry) * m_bpb.root_dir_entries);
    }
    else
    {
        // some constants
        const std::size_t entries_per_cluster = m_bpb.sectors_per_cluster *
                                                m_bpb.bytes_per_sector /
                                                sizeof(directory_entry);

        // obtain FAT table
        binary_type fat   = read_fat();
        const auto  ptr32 = reinterpret_cast<std::uint32_t *>(fat.data());

        directory_type root_directory{};

        // get root directory first cluster index
        std::size_t index   = m_bpb.offset_36.fat32.root_cluster;
        std::size_t reserve = 0;

        do
        {
            // seek to position of cluster in disk
            m_fs.seekg((start_of_root_directory +
                        (index - 2) * m_bpb.sectors_per_cluster) *
                       m_bpb.bytes_per_sector);

            root_directory.resize(reserve += entries_per_cluster);

            // read cluster
            m_fs.read(reinterpret_cast<char *>(root_directory.data() + reserve -
                                               entries_per_cluster),
                      sizeof(directory_entry) * entries_per_cluster);

            // get next cluster index
            index = ptr32[index];
        } while (index < 0x0FFFFFF0);
        // indices greater than 0x0FFFFFF0 are either damaged or indicates the
        // last cluster in the chain
    }

    // get iterator to first null entry
    auto it = std::find_if(root_directory.begin(),
                           root_directory.end(),
                           [](const directory_entry &x) {
                               return x.name[0] == '\0';
                           });

    // remove null entries
    root_directory.erase(it, root_directory.end());

    return root_directory;
}

poc::file_allocation_table::binary_type poc::file_allocation_table::read_fat()
{
    const version fsver = file_system_version();

    const std::size_t sectors_per_fat =
        fsver == version::fat32 ? m_bpb.offset_36.fat32.sectors_per_fat_32
                                : m_bpb.sectors_per_fat_16;
    const std::size_t fat_bytes =
        sectors_per_fat * m_bpb.number_of_fats * m_bpb.bytes_per_sector;

    binary_type fat(fat_bytes);

    m_fs.seekg(m_bpb.reserved_sectors * m_bpb.bytes_per_sector);
    m_fs.read(reinterpret_cast<char *>(fat.data()), fat_bytes);

    return fat;
}

std::vector<poc::directory_entry>
poc::file_allocation_table::read_directory(const std::string_view path)
{
    std::vector<poc::directory_entry> dir{};

    // if root path is given then return root directory
    if (trim_string(path.data()) == "\\")
    {
        return read_root_directory();
    }
    else
    {
        binary_type contents = read_file_internal(path, true);

        const std::byte * ptr = contents.data();
        const std::size_t len = contents.size() / sizeof(directory_entry);
        const auto        arr = reinterpret_cast<const directory_entry *>(ptr);

        dir = std::vector(arr, arr + len);
    }

    // get iterator to first null entry
    auto it =
        std::find_if(dir.begin(), dir.end(), [](const directory_entry &x) {
            return x.name[0] == '\0';
        });

    // remove null entries
    dir.erase(it, dir.end());

    return dir;
}

poc::file_allocation_table::binary_type
poc::file_allocation_table::read_file(const std::string_view path)
{
    return read_file_internal(path, false);
}

poc::file_allocation_table::binary_type
poc::file_allocation_table::read_file_internal(const std::string_view path,
                                               bool is_directory)
{
    if (trim_string(path.data()).empty())
        throw std::runtime_error{"path is empty"};

    const std::string lpath{path};
    std::string       tmp;

    std::stringstream        ss{lpath};
    std::vector<std::string> path_components{};

    // split on backslash
    while (std::getline(ss, tmp, '\\'))
    {
        path_components.emplace_back(
            miscellaneous::convert_normal_to_8_3(trim_string(tmp)));
    }

    path_components.erase(
        std::remove_if(path_components.begin(),
                       path_components.end(),
                       [](const std::string_view x) {
                           return trim_string(x.data()).empty();
                       }),
        path_components.end());
    path_components.shrink_to_fit();

    binary_type contents{};

    directory_type parent = read_root_directory();

    for (std::size_t i = 0; i < path_components.size(); ++i)
    {
        auto &x = path_components[i];

        // find directory entry
        auto entry = std::find_if(
            parent.begin(),
            parent.end(),
            [&](const directory_entry &y) {
                const bool v1 =
                    std::strncmp(x.data(),
                                 reinterpret_cast<const char *>(y.name),
                                 8) == 0;
                const bool v2 =
                    std::strncmp(x.data() + 8,
                                 reinterpret_cast<const char *>(y.extension),
                                 3) == 0;

                // if there are more path components then this one must be a
                // directory otherwise, if is_directory is true then this one
                // must be a directory, else a file
                if (is_directory)
                {
                    return v1 && v2 &&
                           (y.attributes &
                            static_cast<int>(
                                directory_entry::attribute::directory));
                }

                return v1 && v2;
            });

        if (entry == parent.end())
        {
            throw std::runtime_error{"file/directory '" +
                                     miscellaneous::convert_8_3_to_normal(x) +
                                     "' not found"};
        }

        // if entry is file and there are more path components then throw
        // exception
        if (i < path_components.size() - 1 &&
            !(entry->attributes &
              static_cast<int>(directory_entry::attribute::directory)))
        {
            throw std::runtime_error{
                "file '" + miscellaneous::convert_8_3_to_normal(x) +
                "' is not a directory, trying to browse contents of it"};
        }

        const version fsver = file_system_version();

        const std::size_t sectors_per_fat =
            fsver == version::fat32 ? m_bpb.offset_36.fat32.sectors_per_fat_32
                                    : m_bpb.sectors_per_fat_16;
        const std::size_t start_of_root_directory =
            m_bpb.reserved_sectors + m_bpb.number_of_fats * sectors_per_fat;
        const std::size_t start_of_data_region =
            start_of_root_directory + m_bpb.root_dir_entries *
                                          sizeof(directory_entry) /
                                          m_bpb.bytes_per_sector;
        const std::size_t bytes_per_cluster =
            m_bpb.sectors_per_cluster * m_bpb.bytes_per_sector;

        binary_type fat = read_fat();

        std::size_t cluster =
            entry->first_cluster_low | entry->first_cluster_high << 16;
        std::size_t reserve = 0;

        do
        {
            // seek to position of cluster in disk
            m_fs.seekg((start_of_data_region +
                        (cluster - 2) * m_bpb.sectors_per_cluster) *
                       m_bpb.bytes_per_sector);

            contents.resize(reserve += bytes_per_cluster);

            // read cluster
            m_fs.read(reinterpret_cast<char *>(contents.data() + reserve -
                                               bytes_per_cluster),
                      bytes_per_cluster);

            if (fsver == version::fat12)
            {
                auto orig = cluster;

                cluster = cluster * 3 / 2;
                cluster = *reinterpret_cast<std::uint16_t *>(
                    reinterpret_cast<std::uint8_t *>(fat.data()) +
                    cluster); // basically, get 2 bytes from location of FAT +
                              // cluster (no pointer arithmetic)

                if (orig % 2 == 0)
                    cluster &= 0xFFF;
                else
                    cluster >>= 4;
            }
            else if (fsver == version::fat16)
            {
                cluster =
                    reinterpret_cast<std::uint16_t *>(fat.data())[cluster];
            }
            else
            {
                cluster =
                    reinterpret_cast<std::uint32_t *>(fat.data())[cluster];
            }
        } while ((fsver == version::fat12 && cluster < 0xFF0) ||
                 (fsver == version::fat16 && cluster < 0xFFF0) ||
                 (fsver == version::fat32 && cluster < 0x0FFFFFF0));

        // if there are more path components then set parent to contents
        if (i != path_components.size() - 1)
        {
            const std::byte * ptr = contents.data();
            const std::size_t len = contents.size() / sizeof(directory_entry);
            const auto arr = reinterpret_cast<const directory_entry *>(ptr);

            parent = std::vector(arr, arr + len);
        }

        // resize contents to actual size IF it is a file AND ONLY IF it is a
        // file
        if (!is_directory || i < path_components.size() - 1)
            contents = {contents.data(), contents.data() + entry->file_size};
    }

    return contents;
}

poc::file_allocation_table::version
poc::file_allocation_table::file_system_version() const
{
    const std::size_t sectors_per_fat =
        m_bpb.sectors_per_fat_16 == 0 ? m_bpb.offset_36.fat32.sectors_per_fat_32
                                      : m_bpb.sectors_per_fat_16;
    const std::size_t total_sectors = m_bpb.total_sectors_16 == 0
                                          ? m_bpb.total_sectors_32
                                          : m_bpb.total_sectors_16;

    const std::size_t start_of_root_directory =
        m_bpb.reserved_sectors + m_bpb.number_of_fats * sectors_per_fat;
    const std::size_t start_of_data_region =
        start_of_root_directory +
        m_bpb.root_dir_entries
            /* in FAT32, the root directory is part of the data region */
            * sizeof(directory_entry) / m_bpb.bytes_per_sector;
    const std::size_t data_region_size = total_sectors - start_of_data_region;

    const std::size_t total_clusters =
        data_region_size / m_bpb.sectors_per_cluster;

    if (total_clusters < 4085)
        return version::fat12;

    if (total_clusters < 65525)
        return version::fat16;

    return version::fat32;
}

void poc::file_allocation_table::write_file(std::string_view path,
                                            binary_type      data)
{
    if (trim_string(path.data()).empty())
        throw std::runtime_error{"path is empty"};

    // bool exists = true;
    // try
    // {
    //     read_file(path);
    // }
    // catch (...)
    // {
    //     exists = false;
    // }

    // if (exists)
    // {
    //     throw std::runtime_error{
    //         "file " + std::string{path} +
    //         " already exists"}; // easy way out; we'll fix this later
    // }

    const std::string lpath{path};
    std::string       tmp;

    std::stringstream        ss{lpath};
    std::vector<std::string> path_components{};

    // split on backslash
    while (std::getline(ss, tmp, '\\'))
    {
        path_components.emplace_back(
            miscellaneous::convert_normal_to_8_3(trim_string(tmp)));
    }

    path_components.erase(
        std::remove_if(path_components.begin(),
                       path_components.end(),
                       [](const std::string_view x) {
                           return trim_string(x.data()).empty();
                       }),
        path_components.end());
    path_components.shrink_to_fit();

    // get parent directory
    std::ostringstream oss{};
    std::transform(path_components.begin(),
                   path_components.end() - 1,
                   std::ostream_iterator<std::string>(oss, "\\"),
                   [](const auto &x) {
                       return miscellaneous::convert_8_3_to_normal(x);
                   });
    const std::string parent_dir = "\\" + oss.str();
    directory_type    parent     = read_directory(parent_dir);

    const version fsver = file_system_version();

    const std::size_t sectors_per_fat =
        fsver == version::fat32 ? m_bpb.offset_36.fat32.sectors_per_fat_32
                                : m_bpb.sectors_per_fat_16;
    const std::size_t start_of_root_directory =
        m_bpb.reserved_sectors + m_bpb.number_of_fats * sectors_per_fat;
    const std::size_t start_of_data_region =
        start_of_root_directory + m_bpb.root_dir_entries *
                                      sizeof(directory_entry) /
                                      m_bpb.bytes_per_sector;
    const std::size_t bytes_per_cluster =
        m_bpb.sectors_per_cluster * m_bpb.bytes_per_sector;

    const std::string &filename = path_components.back();

    const std::chrono::system_clock::time_point now =
        std::chrono::system_clock::now();
    const std::time_t now_t  = std::chrono::system_clock::to_time_t(now);
    const std::tm *   now_tm = std::localtime(&now_t);

    binary_type fat = read_fat();

    std::uint32_t next = get_next_free_cluster(fat);

    directory_entry::date date;
    directory_entry::time time;

    date.day   = now_tm->tm_mday;
    date.month = now_tm->tm_mon + 1;
    date.year =
        now_tm->tm_year - 80; // 1980 is the base year; (struct tm*)->tm_year is
                              // the number of years since 1900

    time.hour   = now_tm->tm_hour;
    time.minute = now_tm->tm_min;
    time.second = now_tm->tm_sec / 2; // 2 second resolution

    directory_entry entry;

    // copy filename
    for (int i = 0; i < 8; i++)
        entry.name[i] = filename[i];

    // copy extension
    for (int i = 0; i < 3; i++)
        entry.extension[i] = filename[i + 8];

    entry.attributes =
        static_cast<uint8_t>(directory_entry::attribute::archive);

    // set fields
    entry.creation_date          = date;
    entry.creation_time          = time;
    entry.creation_time_tenths   = 0;
    entry.last_access_date       = date;
    entry.first_cluster_high     = next >> 16; // high 16 bits
    entry.last_modification_time = time;
    entry.last_modification_date = date;
    entry.first_cluster_low      = next & 0xFFFF; // low 16 bits
    entry.file_size              = data.size();

    const std::size_t data_size_in_bytes_rounded =
        (data.size() + bytes_per_cluster - 1) / bytes_per_cluster *
        bytes_per_cluster;
    const std::size_t data_size_in_clusters =
        data_size_in_bytes_rounded / bytes_per_cluster;

    std::vector<std::vector<std::byte>> cluster_division{}; // data divided into
                                                            // clusters
    cluster_division.reserve(data_size_in_clusters);

    for (std::size_t i = 0; i < data_size_in_clusters; i++)
    {
        std::vector<std::byte> cluster{};
        cluster.reserve(bytes_per_cluster);

        for (std::size_t j = 0; j < bytes_per_cluster; j++)
        {
            // copy normally until we run out of data, then we pad with 0s
            if (i * bytes_per_cluster + j < data.size())
                cluster.emplace_back(data[i * bytes_per_cluster + j]);
            else
                cluster.emplace_back(std::byte{0});
        }

        cluster_division.emplace_back(cluster);
    }

    // write FAT clusters
    std::vector<uint32_t> saved_clusters{};
    saved_clusters.reserve(data_size_in_clusters);

    uint32_t end_of_chain_marker;
    switch (fsver)
    {
    case version::fat12: end_of_chain_marker = 0x0FFF; break;
    case version::fat16: end_of_chain_marker = 0xFFFF; break;
    case version::fat32: end_of_chain_marker = 0x0FFFFFFF; break;
    }

    // write clusters to buffer
    for (std::size_t i = 0; i < data_size_in_clusters; i++)
    {
        std::uint32_t current = next;
        next                  = i < data_size_in_clusters - 1
                                    ? get_next_free_cluster(fat, current)
                                    : end_of_chain_marker;

        switch (fsver)
        {
        case version::fat12:
        {
            std::size_t cluster = current * 3 / 2;
            uint8_t *   ptr8    = reinterpret_cast<uint8_t *>(fat.data());
            uint16_t *  cluster_ptr =
                reinterpret_cast<uint16_t *>(ptr8 + cluster);

            if (current % 2 == 0)
                *cluster_ptr |= next & 0x0FFF; // store in low 12 bits if even
            else
                *cluster_ptr |= next << 4; // else, store in high 12 bits
        }
        break;
        case version::fat16:
        {
            std::size_t cluster                               = current;
            reinterpret_cast<uint16_t *>(fat.data())[cluster] = next;
        }
        break;
        case version::fat32:
        {
            std::size_t cluster                               = current;
            reinterpret_cast<uint32_t *>(fat.data())[cluster] = next;
        }
        break;
        }

        saved_clusters.emplace_back(current);
    }

    parent.emplace_back(entry);

    // get cluster of self
    std::uint32_t self_cluster =
        parent[0].first_cluster_low |
        (parent[0].first_cluster_high << 16); // first entry is always . (self)

    // write directory
    m_fs.seekp((start_of_data_region +
                (self_cluster - 2) * m_bpb.sectors_per_cluster) *
               m_bpb.bytes_per_sector);
    m_fs.write(reinterpret_cast<const char *>(parent.data()),
               parent.size() * sizeof(directory_entry));

    // write FAT
    m_fs.seekp(m_bpb.reserved_sectors * m_bpb.bytes_per_sector);
    m_fs.write(reinterpret_cast<const char *>(fat.data()), fat.size());

    // write data
    int i = 0;
    for (const auto &cluster : saved_clusters)
    {
        m_fs.seekp(
            (start_of_data_region + (cluster - 2) * m_bpb.sectors_per_cluster) *
            m_bpb.bytes_per_sector);
        m_fs.write(reinterpret_cast<const char *>(cluster_division[i].data()),
                   bytes_per_cluster);

        i++;
    }
}

std::uint32_t
poc::file_allocation_table::get_next_free_cluster(const binary_type &fat,
                                                  uint32_t start_cluster)
{
    const version fsver = file_system_version();

    switch (fsver)
    {
    case version::fat12:
    {
        for (std::size_t i = start_cluster + 1; i < fat.size() * 2 / 3; i++)
        {
            auto        orig    = i;
            std::size_t cluster = i * 3 / 2;
            cluster             = *reinterpret_cast<const uint16_t *>(
                reinterpret_cast<const std::uint8_t *>(fat.data()) + cluster);

            if (orig % 2 == 0)
                cluster &= 0xFFF;
            else
                cluster >>= 4;

            if (cluster == 0)
                return i;
        }
    }
    break;
    case version::fat16:
    {
        for (std::size_t i = start_cluster + 1;
             i < fat.size() / 2 /* sizeof(uint16_t) */;
             i++)
        {
            std::uint16_t cluster =
                reinterpret_cast<const uint16_t *>(fat.data())[i];

            if (cluster == 0)
                return i;
        }
    }
    break;
    default /* FAT32 */:
    {
        for (std::size_t i = start_cluster + 1;
             i < fat.size() / 4 /* sizeof(uint32_t) */;
             i++)
        {
            std::uint32_t cluster =
                reinterpret_cast<const uint32_t *>(fat.data())[i];

            if (cluster == 0)
                return i;
        }
    }
    break;
    }

    return 0;
}

std::string poc::miscellaneous::convert_normal_to_8_3(std::string_view name)
{
    std::string r = name.data();
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

std::string poc::miscellaneous::convert_8_3_to_normal(std::string_view name)
{
    std::string r = name.data();
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
                              [](const char c) {
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
                              [](const char c) {
                                  return c != ' ';
                              })
                     .base(),
                 result.end());

    return result;
}

namespace
{
std::string trim_string(const std::string &str)
{
    std::string result = str;
    result.erase(std::find_if(result.rbegin(),
                              result.rend(),
                              [](const char c) {
                                  return c != ' ';
                              })
                     .base(),
                 result.end());
    return result;
}
} // namespace
