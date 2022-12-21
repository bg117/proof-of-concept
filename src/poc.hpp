#pragma once

#include <bits/c++config.h>
#include <bits/stdint-uintn.h>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <string_view>
#include <vector>

namespace poc
{
enum directory_entry_attr
{
    read_only = 0x01,
    hidden    = 0x02,
    system    = 0x04,
    volume_id = 0x08,
    directory = 0x10,
    archive   = 0x20,
    long_name = read_only | hidden | system | volume_id
};

enum class file_system_version
{
    fat12,
    fat16,
    fat32
};

struct directory_entry
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
#pragma pack(push, 1)
    struct _Bios_parameter_block
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

    enum _Dir_entry_attr
    {
        read_only = 0x01,
        hidden    = 0x02,
        system    = 0x04,
        volume_id = 0x08,
        directory = 0x10,
        archive   = 0x20,
        long_name = read_only | hidden | system | volume_id
    };

    struct _Dir_entry_date_fmt
    {
        uint8_t day   : 5;
        uint8_t month : 4;
        uint8_t year  : 7; // since 1980
    };

    struct _Dir_entry_time_fmt
    {
        uint8_t second : 5;
        uint8_t minute : 6;
        uint8_t hour   : 5;
    };

    struct _Dir_entry
    {

        uint8_t             name[8];
        uint8_t             extension[3];
        uint8_t             attributes;
        uint8_t             reserved;
        uint8_t             creation_time_tenths;
        _Dir_entry_time_fmt creation_time;
        _Dir_entry_date_fmt creation_date;
        _Dir_entry_date_fmt last_access_date;
        uint16_t            first_cluster_high;
        _Dir_entry_time_fmt last_modification_time;
        _Dir_entry_date_fmt last_modification_date;
        uint16_t            first_cluster_low;
        uint32_t            file_size;
    };
#pragma pack(pop)

  public:
    explicit file_allocation_table(std::string_view path);

    std::vector<directory_entry> read_directory(std::string_view path);
    std::vector<std::byte>       read_file(std::string_view path);

    void write_file(std::string_view path, const std::vector<std::byte> &data);

    file_system_version version() const;

    // miscellaneous
    static std::string convert_normal_to_8_3(std::string_view name);
    static std::string convert_8_3_to_normal(std::string_view name);

  private:
    std::fstream m_fs;

    // do not modify
    // {
    file_system_version m_version;

    _Bios_parameter_block   m_bpb;
    std::vector<std::byte>  m_fat;
    std::vector<_Dir_entry> m_root_dir;

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

    std::vector<std::byte> _Read_file_internal(std::string_view path,
                                               bool             is_directory);

    std::vector<_Dir_entry> _Read_directory_raw(std::string_view path);

    std::size_t _Get_cluster(std::size_t cluster_number) const;
    void        _Set_cluster(std::size_t cluster_number, std::size_t next);

    std::vector<std::size_t> _Get_cluster_chain(std::size_t start_cluster);

    std::size_t _Get_next_free_cluster(
        std::size_t start_cluster = 1 /* start_cluster + 1 == 2*/);

    std::size_t _Get_sector_from_cluster(std::size_t cluster) const;

    std::uint16_t
    _Get_uint16_t_from_timestamp(const _Dir_entry_time_fmt &time_format) const;

    std::uint16_t
    _Get_uint16_t_from_timestamp(const _Dir_entry_date_fmt &date_format) const;

    bool _Is_end_of_chain(std::size_t cluster) const;
};
} // namespace poc
