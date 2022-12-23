#include "poc.hpp"
#include "fs_errors.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

// why not? this stuff clutters the header file when it's only ever used within
// this translation unit
#pragma pack(push, 1)
struct bios_parameter_block
{
    uint8_t  jmp[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  number_of_fats;
    uint16_t root_dir_entries;
    uint16_t total_sectors_16;
    uint8_t  media_descriptor;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t number_of_heads;
    uint32_t number_of_hidden_sectors;
    uint32_t total_sectors_32;

    union
    {
        struct
        {
            uint8_t  drive_number;
            uint8_t  reserved;
            uint8_t  boot_signature;
            uint32_t volume_id;
            uint8_t  volume_label[11];
            uint8_t  file_system_type[8];
        } fat12_16;

        struct
        {
            uint32_t sectors_per_fat_32;
            uint16_t extended_flags;
            uint16_t fs_version;
            uint32_t root_cluster;
            uint16_t fs_info;
            uint16_t backup_boot_sector;
            uint8_t  zero[12];
            uint8_t  drive_number;
            uint8_t  reserved;
            uint8_t  boot_signature;
            uint32_t volume_id;
            uint8_t  volume_label[11];
            uint8_t  file_system_type[8];
        } fat32;
    } offset_36;
};

enum dir_entry_attr
{
    read_only   = 0x01,
    hidden      = 0x02,
    system_file = 0x04,
    volume_id   = 0x08,
    directory   = 0x10,
    archive     = 0x20,
    long_name   = read_only | hidden | system_file | volume_id
};

struct dir_entry_date_fmt
{
    uint8_t day   : 5;
    uint8_t month : 4;
    uint8_t year  : 7; // since 1980
};

struct dir_entry_time_fmt
{
    uint8_t second : 5;
    uint8_t minute : 6;
    uint8_t hour   : 5;
};

struct dir_entry
{

    uint8_t            name[8];
    uint8_t            extension[3];
    uint8_t            attributes;
    uint8_t            reserved;
    uint8_t            creation_time_tenths;
    dir_entry_time_fmt creation_time;
    dir_entry_date_fmt creation_date;
    dir_entry_date_fmt last_access_date;
    uint16_t           first_cluster_high;
    dir_entry_time_fmt last_modification_time;
    dir_entry_date_fmt last_modification_date;
    uint16_t           first_cluster_low;
    uint32_t           file_size;
};

#pragma pack(pop)
struct poc::file_allocation_table::impl
{
  public:
    impl(std::string_view path);

    std::vector<file_info> read_directory(std::string_view path);
    std::vector<std::byte> read_file(std::string_view path);

    void write_file(std::string_view path, const std::vector<std::byte> &data);

  private:
    std::fstream m_fs;

    // do not modify
    // {
    file_system_version m_version;

    bios_parameter_block   m_bpb;
    std::vector<std::byte> m_fat;

    std::size_t m_sectors_per_fat;
    std::size_t m_total_fat_sectors;
    std::size_t m_root_dir_sectors;
    std::size_t m_data_sectors;
    std::size_t m_first_root_dir_sector;
    std::size_t m_first_data_sector;
    std::size_t m_first_fat_sector;
    std::size_t m_total_sectors;
    std::size_t m_total_clusters;
    std::size_t m_bytes_per_cluster;

    std::size_t m_end_of_chain;
    // }

    std::vector<std::byte> read_file(std::string_view path, bool is_directory);

    std::vector<dir_entry> read_raw_directory(std::string_view path);

    std::size_t extract_cluster(std::size_t cluster_number) const;
    void        set_cluster(std::size_t cluster_number, std::size_t next);

    std::vector<std::size_t>
    extract_cluster_chain(std::size_t start_cluster) const;

    std::size_t get_next_free_cluster(
        std::size_t start_cluster = 1 /* start_cluster + 1 == 2*/) const;

    std::size_t convert_cluster_to_sector(std::size_t cluster) const;

    bool is_end_of_cluster_chain(std::size_t cluster) const;
};

namespace
{
std::string trim_string(const std::string &str);

std::string convert_normal_to_8_3(std::string_view name);
std::string convert_8_3_to_normal(std::string_view name);

std::vector<std::string> convert_path_to_fat_path(std::string_view path);

std::time_t convert_fat_to_unix_time(dir_entry_time_fmt date,
                                     dir_entry_date_fmt time);

constexpr int round_up(int num, int multiple);

constexpr bool is_bit_set(int seq, int bit);
} // namespace

poc::file_allocation_table::file_allocation_table(std::string_view path)
{
    m_impl = std::make_unique<impl>(path);
}

poc::file_allocation_table::~file_allocation_table() = default;

std::vector<poc::file_info>
poc::file_allocation_table::read_directory(const std::string_view path)
{
    return m_impl->read_directory(path);
}

std::vector<std::byte>
poc::file_allocation_table::read_file(const std::string_view path)
{
    return m_impl->read_file(path);
}

void poc::file_allocation_table::write_file(std::string_view              path,
                                            const std::vector<std::byte> &data)
{
    m_impl->write_file(path, data);
}

poc::file_allocation_table::impl::impl(std::string_view path)
    : m_fs(), m_bpb(), m_fat()
{
    m_fs.open(path.data(), std::ios::binary | std::ios::in | std::ios::out);
    if (!m_fs.is_open())
        throw std::runtime_error{"failed to open file " + std::string{path}};

    // copy BPB to struct
    m_fs.seekg(0);
    m_fs.read(reinterpret_cast<char *>(&m_bpb), sizeof m_bpb);

    // fill in missing fields
    m_sectors_per_fat   = m_bpb.sectors_per_fat_16 == 0
                              ? m_bpb.offset_36.fat32.sectors_per_fat_32
                              : m_bpb.sectors_per_fat_16;
    m_total_fat_sectors = m_sectors_per_fat * m_bpb.number_of_fats;
    m_total_sectors     = m_bpb.total_sectors_16 == 0 ? m_bpb.total_sectors_32
                                                      : m_bpb.total_sectors_16;

    m_first_fat_sector = m_bpb.reserved_sectors;
    m_first_root_dir_sector =
        m_first_fat_sector + m_bpb.number_of_fats * m_sectors_per_fat;

    m_first_data_sector =
        m_first_root_dir_sector +
        m_bpb.root_dir_entries
            /* in FAT32, the root directory is part of the data region */
            * sizeof(dir_entry) / m_bpb.bytes_per_sector;

    m_data_sectors   = m_total_sectors - m_first_data_sector;
    m_total_clusters = m_data_sectors / m_bpb.sectors_per_cluster;

    m_bytes_per_cluster = m_bpb.sectors_per_cluster * m_bpb.bytes_per_sector;

    if (m_total_clusters < 4085)
        m_version = file_system_version::fat12;
    else if (m_total_clusters < 65525)
        m_version = file_system_version::fat16;
    else
        m_version = file_system_version::fat32;

    // copy FAT table to struct
    m_fat.resize(m_total_fat_sectors * m_bpb.bytes_per_sector);

    m_fs.seekg(m_first_fat_sector * m_bpb.bytes_per_sector);
    m_fs.read(reinterpret_cast<char *>(m_fat.data()), m_fat.size());

    m_end_of_chain = extract_cluster(1); // end of chain marker is stored in the
                                         // second entry of the FAT table

    if (m_version == file_system_version::fat32)
    {
        m_end_of_chain &= 0x0FFFFFFF; // some weird software fills the upper 4
                                      // bits, so we need to mask them out
    }
}

std::vector<poc::file_info>
poc::file_allocation_table::impl::read_directory(const std::string_view path)
{
    std::vector<file_info> dir{}; // user-readable directory
    std::vector<dir_entry> raw_dir =
        read_raw_directory(path); // "raw" directory (as it is on disk)

    // convert to user-readable directory
    std::transform(
        raw_dir.begin(),
        raw_dir.end(),
        std::back_inserter(dir),
        [&](const dir_entry &x) {
            // convert name and extension to "normal" format
            std::string name =
                // 8 characters for name
                std::string(reinterpret_cast<const char *>(x.name),
                            std::size(x.name)) +
                // 3 characters for extension
                std::string(reinterpret_cast<const char *>(x.extension),
                            std::size(x.extension));

            return file_info{
                .name = convert_8_3_to_normal(name),
                .creation_timestamp =
                    convert_fat_to_unix_time(x.creation_time, x.creation_date),

                .last_modification_timestamp =
                    convert_fat_to_unix_time(x.last_modification_time,
                                             x.last_modification_date),

                .last_access_date =
                    convert_fat_to_unix_time({}, x.last_access_date),

                .size = x.file_size,

                .is_directory =
                    is_bit_set(x.attributes, dir_entry_attr::directory),
            };
        });

    return dir;
}

std::vector<std::byte>
poc::file_allocation_table::impl::read_file(const std::string_view path)
{
    return read_file(path, false);
}

void poc::file_allocation_table::impl::write_file(
    std::string_view              path,
    const std::vector<std::byte> &data)
{
    if (trim_string(path.data()).empty())
        throw errors::invalid_path_error{"path is empty"};

    bool exists = true;
    try
    {
        read_file(path);
    }
    catch (const errors::file_not_found_error &)
    {
        exists = false;
    }

    if (exists)
    {
        throw errors::file_already_exists_error{
            "file " + std::string{path} +
            " already exists"}; // easy way out; we'll fix this later
    }

    std::vector<std::string> path_components = convert_path_to_fat_path(path);

    // get parent directory
    std::ostringstream oss{};
    std::transform(path_components.begin(),
                   path_components.end() - 1,
                   std::ostream_iterator<std::string>(oss, "\\"),
                   convert_8_3_to_normal);
    const std::string      parent_dir = "\\" + oss.str();
    std::vector<dir_entry> parent     = read_raw_directory(parent_dir);

    const std::size_t m_bytes_per_cluster =
        m_bpb.sectors_per_cluster * m_bpb.bytes_per_sector;

    const std::string &filename = path_components.back();

    const std::chrono::system_clock::time_point now =
        std::chrono::system_clock::now();
    const std::time_t now_t  = std::chrono::system_clock::to_time_t(now);
    const std::tm *   now_tm = std::localtime(&now_t);

    std::size_t next = get_next_free_cluster();

    dir_entry_date_fmt date;
    dir_entry_time_fmt time;

    date.day   = now_tm->tm_mday;
    date.month = now_tm->tm_mon + 1;
    date.year =
        now_tm->tm_year - 80; // 1980 is the base year; (struct tm*)->tm_year is
                              // the number of years since 1900

    time.hour   = now_tm->tm_hour;
    time.minute = now_tm->tm_min;
    time.second = now_tm->tm_sec / 2; // 2 second resolution

    dir_entry entry;

    // copy filename
    for (int i = 0; i < 8; i++)
        entry.name[i] = filename[i];

    // copy extension
    for (int i = 0; i < 3; i++)
        entry.extension[i] = filename[i + 8];

    entry.attributes = static_cast<uint8_t>(dir_entry_attr::archive);

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
        round_up(data.size(), m_bytes_per_cluster);
    const std::size_t data_size_in_clusters =
        data_size_in_bytes_rounded / m_bytes_per_cluster;

    std::vector<std::vector<std::byte>> cluster_division{}; // data divided into
                                                            // clusters
    cluster_division.reserve(data_size_in_clusters);

    for (std::size_t i = 0; i < data_size_in_clusters; i++)
    {
        std::vector<std::byte> cluster{};
        cluster.reserve(m_bytes_per_cluster);

        for (std::size_t j = 0; j < m_bytes_per_cluster; j++)
        {
            // copy normally until we run out of data, then we pad with 0s
            if (i * m_bytes_per_cluster + j < data.size())
                cluster.emplace_back(data[i * m_bytes_per_cluster + j]);
            else
                cluster.emplace_back(std::byte{0});
        }

        cluster_division.emplace_back(cluster);
    }

    // write FAT clusters
    std::vector<std::size_t> saved_clusters{};
    saved_clusters.reserve(data_size_in_clusters);

    std::size_t end_of_chain_marker;
    switch (m_version)
    {
    case file_system_version::fat12: end_of_chain_marker = 0x0FFF; break;
    case file_system_version::fat16: end_of_chain_marker = 0xFFFF; break;
    case file_system_version::fat32: end_of_chain_marker = 0x0FFFFFFF; break;
    }

    // write clusters to buffer
    for (std::size_t i = 0; i < data_size_in_clusters; i++)
    {
        std::size_t current = next;

        // if we're at the last cluster, we need to set the end of chain marker
        next = i < data_size_in_clusters - 1 ? get_next_free_cluster(current)
                                             : end_of_chain_marker;

        set_cluster(current, next);
        saved_clusters.emplace_back(current);
    }

    const std::size_t old_dir_size = parent.size();

    parent.emplace_back(entry);

    const std::size_t new_dir_size =
        parent.size(); // check if this one exceeds cluster boundary

    // get cluster of self
    std::size_t self_cluster =
        parent[0].name[0] == '.'
            ? parent[0].first_cluster_high << 16 | parent[0].first_cluster_low
            : 0; // if we're in the root directory, we don't have a cluster

    // check if exceeding the maximum number of entries in a directory IF
    // we're in the root directory and IF we're in a non-FAT32 volume
    if (self_cluster == 0 && m_version != file_system_version::fat32)
    {
        if (parent.size() > m_bpb.root_dir_entries)
        {
            throw errors::file_system_error{"maximum number of entries in root "
                                            "directory exceeded"};
        }
    }

    auto p = self_cluster;

    // if root directory AND FAT32, get first cluster of root directory
    if (self_cluster == 0 && m_version == file_system_version::fat32)
        self_cluster = m_bpb.offset_36.fat32.root_cluster;

    // get sector from cluster
    const std::size_t self_lba = p == 0
                                     ? m_first_root_dir_sector
                                     : convert_cluster_to_sector(self_cluster);

    // only perform directory checks if either
    //   - we're not in the root directory
    //   - we're in the root directory and we're in a FAT32 volume
    if (p >= 2 || (p == 0 && m_version == file_system_version::fat32))
    {
        std::vector<std::size_t> dir_cluster_chain =
            extract_cluster_chain(self_cluster);

        const std::size_t entries_per_cluster =
            m_bytes_per_cluster / sizeof(dir_entry);

        // if we're exceeding the cluster boundary, we need to allocate a new
        // cluster
        if (round_up(new_dir_size, m_bytes_per_cluster) >
            round_up(old_dir_size, m_bytes_per_cluster))
        {
            std::size_t new_cluster = get_next_free_cluster(self_cluster);
            dir_cluster_chain.emplace_back(new_cluster);
            set_cluster(self_cluster, new_cluster);
            set_cluster(new_cluster, end_of_chain_marker);
        }

        const std::size_t new_dir_clusters =
            round_up(new_dir_size, m_bytes_per_cluster) / m_bytes_per_cluster;

        // pad directory with 0s up until the next cluster boundary
        parent.resize(entries_per_cluster * new_dir_clusters);

        int i = 0;

        // write directory
        for (const auto &cluster : dir_cluster_chain)
        {
            m_fs.seekp(convert_cluster_to_sector(cluster) *
                       m_bpb.bytes_per_sector);
            m_fs.write(reinterpret_cast<const char *>(parent.data() +
                                                      i * entries_per_cluster),
                       m_bytes_per_cluster);
            i++;
        }
    }
    else // we're in the root directory AND we're in a non-FAT32 volume
         // AND (thankfully) we're not exceeding the maximum number of entries
    {
        parent.resize(m_bpb.root_dir_entries);

        // write directory
        m_fs.seekp(self_lba * m_bpb.bytes_per_sector);
        m_fs.write(reinterpret_cast<const char *>(parent.data()),
                   parent.size() * sizeof(dir_entry));
    }

    // write FAT
    for (int i = 0; i < m_bpb.number_of_fats; i++)
    {
        m_fs.seekp((m_first_fat_sector + i * m_sectors_per_fat) *
                   m_bpb.bytes_per_sector);
        m_fs.write(reinterpret_cast<const char *>(m_fat.data()),
                   m_sectors_per_fat * m_bpb.bytes_per_sector);
    }

    // write data
    for (int i = 0; i < saved_clusters.size(); i++)
    {
        m_fs.seekp(convert_cluster_to_sector(saved_clusters[i]) *
                   m_bpb.bytes_per_sector);
        m_fs.write(reinterpret_cast<const char *>(cluster_division[i].data()),
                   m_bytes_per_cluster);
    }
}

std::vector<std::byte>
poc::file_allocation_table::impl::read_file(const std::string_view path,
                                            bool                   is_directory)
{
    if (trim_string(path.data()).empty())
        throw errors::invalid_path_error{"path is empty"};

    std::vector<std::string> path_components = convert_path_to_fat_path(path);

    std::vector<std::byte> contents{};
    std::vector<dir_entry> parent =
        read_raw_directory("\\"); // read root directory

    for (std::size_t i = 0; i < path_components.size(); ++i)
    {
        auto &x = path_components[i];

        // find directory entry
        auto entry =
            std::find_if(parent.begin(), parent.end(), [&](const dir_entry &y) {
                const bool v1 = // compare first 8 characters
                    std::strncmp(x.data(),
                                 reinterpret_cast<const char *>(y.name),
                                 8) == 0;
                const bool v2 = // compare last 3 characters
                    std::strncmp(x.data() + 8,
                                 reinterpret_cast<const char *>(y.extension),
                                 3) == 0;

                // if there are more path components then this one must be a
                // directory otherwise, if is_directory is true then this one
                // must be a directory, else a file
                if (i < path_components.size() - 1 || is_directory)
                {
                    return v1 && v2 &&
                           (y.attributes &
                            static_cast<int>(dir_entry_attr::directory));
                }

                return v1 && v2;
            });

        if (entry == parent.end())
        {
            if (i < path_components.size() - 1 || is_directory)
            {
                throw errors::directory_not_found_error{
                    "directory '" + convert_8_3_to_normal(x) + "' not found"};
            }
            else
            {
                throw errors::file_not_found_error{
                    "file '" + convert_8_3_to_normal(x) + "' not found"};
            }
        }

        // if entry is file and there are more path components then
        // throwm_version exception
        if (i < path_components.size() - 1 &&
            !(entry->attributes & static_cast<int>(dir_entry_attr::directory)))
        {
            throw errors::invalid_file_operation_error{
                "file '" + convert_8_3_to_normal(x) +
                "' is not a directory, trying to browse contents of it"};
        }

        std::size_t cluster =
            entry->first_cluster_low | entry->first_cluster_high << 16;
        std::size_t new_size = 0;

        do
        {
            // seek to position of cluster in disk
            m_fs.seekg(convert_cluster_to_sector(cluster) *
                       m_bpb.bytes_per_sector);

            new_size += m_bytes_per_cluster;
            contents.resize(new_size);

            // read cluster
            m_fs.read(reinterpret_cast<char *>(contents.data() + new_size -
                                               m_bytes_per_cluster),
                      m_bytes_per_cluster);

            cluster = extract_cluster(cluster);
        } while (!is_end_of_cluster_chain(cluster));

        // if there are more path components then set parent to contents
        if (i != path_components.size() - 1)
        {
            const std::byte * ptr = contents.data();
            const std::size_t len = contents.size() / sizeof(dir_entry);
            const auto        arr = reinterpret_cast<const dir_entry *>(ptr);

            parent = std::vector(arr, arr + len);

            // get iterator to first null entry
            auto it = std::find_if(parent.begin(),
                                   parent.end(),
                                   [](const dir_entry &x) {
                                       return x.name[0] == '\0';
                                   });

            // remove null entries
            parent.erase(it, parent.end());
        }

        // resize contents to actual size IF it is a file AND ONLY IF it is a
        // file
        if (!(i < path_components.size() - 1) && !is_directory)
            contents = {contents.data(), contents.data() + entry->file_size};
    }

    return contents;
}

std::size_t poc::file_allocation_table::impl::extract_cluster(
    std::size_t cluster_number) const
{
    std::size_t cluster = cluster_number;

    switch (m_version)
    {
    case file_system_version::fat12:
    {
        auto orig = cluster;

        cluster = cluster * 3 / 2;
        cluster = *reinterpret_cast<const std::uint16_t *>(
            reinterpret_cast<const std::uint8_t *>(m_fat.data()) +
            cluster); // basically, get 2 bytes from location of FAT +
                      // cluster (no pointer arithmetic)

        if (orig % 2 == 0)
            cluster &= 0xFFF;
        else
            cluster >>= 4;
    }
    break;
    case file_system_version::fat16:
        cluster =
            reinterpret_cast<const std::uint16_t *>(m_fat.data())[cluster];
        break;
    case file_system_version::fat32:
        cluster =
            reinterpret_cast<const std::uint32_t *>(m_fat.data())[cluster];
        break;
    }

    return cluster;
}

void poc::file_allocation_table::impl::set_cluster(std::size_t cluster_number,
                                                   std::size_t next)
{
    std::size_t cluster = cluster_number;

    switch (m_version)
    {
    case file_system_version::fat12:
    {
        cluster          = cluster * 3 / 2;
        auto ptr8        = reinterpret_cast<uint8_t *>(m_fat.data());
        auto cluster_ptr = reinterpret_cast<uint16_t *>(ptr8 + cluster);

        if (cluster % 2 == 0)
            *cluster_ptr |= next << 4; // if even, store in high 12 bits
        else
            *cluster_ptr |= next & 0x0FFF; // else, store in low 12 bits
    }
    break;
    case file_system_version::fat16:
        reinterpret_cast<uint16_t *>(m_fat.data())[cluster] = next;
        break;
    case file_system_version::fat32:
        reinterpret_cast<uint32_t *>(m_fat.data())[cluster] = next;
        break;
    }
}

std::vector<std::size_t>
poc::file_allocation_table::impl::extract_cluster_chain(
    std::size_t start_cluster) const
{
    std::vector<std::size_t> chain;
    std::size_t              cluster = start_cluster;

    // pretty self-explanatory
    do
    {
        chain.emplace_back(cluster);
        cluster = extract_cluster(cluster);
    } while (!is_end_of_cluster_chain(cluster));

    return chain;
}

std::size_t poc::file_allocation_table::impl::get_next_free_cluster(
    std::size_t start_cluster) const
{

    switch (m_version)
    {
    case file_system_version::fat12:
    {
        for (std::size_t i = start_cluster + 1; i < m_fat.size() * 2 / 3; i++)
        {
            auto        orig    = i;
            std::size_t cluster = i * 3 / 2;
            cluster             = *reinterpret_cast<const uint16_t *>(
                reinterpret_cast<const std::uint8_t *>(m_fat.data()) + cluster);

            if (orig % 2 == 0)
                cluster &= 0xFFF;
            else
                cluster >>= 4;

            if (cluster == 0)
                return i;
        }
    }
    break;
    case file_system_version::fat16:
    {
        for (std::size_t i = start_cluster + 1;
             i < m_fat.size() / 2 /* sizeof(uint16_t) */;
             i++)
        {
            std::uint16_t cluster =
                reinterpret_cast<const uint16_t *>(m_fat.data())[i];

            if (cluster == 0)
                return i;
        }
    }
    break;
    default /* FAT32 */:
    {
        for (std::size_t i = start_cluster + 1;
             i < m_fat.size() / 4 /* sizeof(uint32_t) */;
             i++)
        {
            std::uint32_t cluster =
                reinterpret_cast<const uint32_t *>(m_fat.data())[i];

            if (cluster == 0)
                return i;
        }
    }
    break;
    }

    return 0;
}

std::vector<dir_entry>
poc::file_allocation_table::impl::read_raw_directory(std::string_view path)
{
    std::vector<dir_entry> raw_dir{}; // "raw" directory (as it is on disk)

    // if root path is given then return root directory
    if (trim_string(path.data()) == "\\")
    {
        if (m_version != file_system_version::fat32)
        {
            // root directory in FAT12 and FAT16 has a fixed size and is located
            // at a fixed offset (directly after the FAT table)
            raw_dir.resize(m_bpb.root_dir_entries * sizeof(dir_entry));

            m_fs.seekg(m_first_root_dir_sector * m_bpb.bytes_per_sector);
            m_fs.read(reinterpret_cast<char *>(raw_dir.data()), raw_dir.size());
        }
        else
        {
            const std::size_t entries_per_cluster =
                m_bpb.sectors_per_cluster * m_bpb.bytes_per_sector /
                sizeof(dir_entry); // just for readability

            std::size_t cluster  = m_bpb.offset_36.fat32.root_cluster;
            std::size_t new_size = 0;

            do
            {
                // seek to position of cluster in disk
                m_fs.seekg(convert_cluster_to_sector(cluster) *
                           m_bpb.bytes_per_sector);

                new_size += entries_per_cluster;
                raw_dir.resize(new_size);

                // read cluster
                m_fs.read(reinterpret_cast<char *>(raw_dir.data() + new_size -
                                                   entries_per_cluster),
                          sizeof(dir_entry) * entries_per_cluster);

                // get next cluster index
                cluster = extract_cluster(cluster);
            } while (cluster < 0x0FFFFFF0);
            // indices greater than 0x0FFFFFF0 are either damaged or indicates
            // the last cluster in the chain
        }
    }
    else
    {
        std::vector<std::byte> contents = read_file(path, true);

        const std::byte * ptr = contents.data(); // pointer to contents
        const std::size_t len =
            contents.size() / sizeof(dir_entry); // amount of entries
        const auto arr = reinterpret_cast<const dir_entry *>(ptr); // cast to
                                                                   // array of
                                                                   // dir_entry

        raw_dir.assign(arr, arr + len);
    }

    // get iterator to first null entry
    auto it =
        std::find_if(raw_dir.begin(), raw_dir.end(), [](const dir_entry &x) {
            return x.name[0] == '\0';
        });

    // remove null entries
    raw_dir.erase(it, raw_dir.end());

    return raw_dir;
}

std::size_t poc::file_allocation_table::impl::convert_cluster_to_sector(
    std::size_t cluster) const
{
    return (cluster - 2) * m_bpb.sectors_per_cluster + m_first_data_sector;
}

bool poc::file_allocation_table::impl::is_end_of_cluster_chain(
    std::size_t cluster) const
{
    bool is_eoc      = cluster == m_end_of_chain;
    bool is_reserved = ((m_version == file_system_version::fat12 &&
                         cluster >= 0x0FF0 && cluster <= 0x0FFF) ||
                        (m_version == file_system_version::fat16 &&
                         cluster >= 0xFFF0 && cluster <= 0xFFFF) ||
                        (m_version == file_system_version::fat32 &&
                         cluster >= 0x0FFFFFF0 && cluster <= 0x0FFFFFFF));

    return is_eoc || is_reserved;
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

std::string convert_normal_to_8_3(std::string_view name)
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

std::string convert_8_3_to_normal(std::string_view name)
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

std::time_t convert_fat_to_unix_time(dir_entry_time_fmt time,
                                     dir_entry_date_fmt date)
{
    std::tm t = {
        .tm_sec = time.second *
                  2, // multiply by 2 because FAT time is in 2 second intervals
        .tm_min  = time.minute,
        .tm_hour = time.hour,
        .tm_mday = date.day,
        .tm_mon  = date.month -
                  1, // subtract 1 because FAT month is 1-12 and m_mon is 0-11
        .tm_year = date.year + 80 // add 80 because FAT year starts from 1980
                                  // and tm_year is 1900-...
    };

    return std::mktime(&t);
}

std::vector<std::string> convert_path_to_fat_path(std::string_view path)
{
    const std::string lpath{path};
    std::string       tmp;

    std::stringstream        ss{lpath};
    std::vector<std::string> path_components{};

    // split on backslash
    while (std::getline(ss, tmp, '\\'))
        path_components.emplace_back(convert_normal_to_8_3(trim_string(tmp)));

    path_components.erase(
        std::remove_if(path_components.begin(),
                       path_components.end(),
                       [](const std::string_view x) {
                           return trim_string(x.data()).empty();
                       }),
        path_components.end());
    path_components.shrink_to_fit();

    return path_components;
}

constexpr int round_up(int num, int multiple)
{
    return ((num + multiple - 1) / multiple) * multiple;
}

constexpr bool is_bit_set(int seq, int bit)
{
    return (seq & bit) == bit;
}
} // namespace
