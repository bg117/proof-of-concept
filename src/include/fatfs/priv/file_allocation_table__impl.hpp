#pragma once

#include "priv.hpp"

#include <fstream>
#include <vector>

FATFS_BEGIN_PRIV_NS

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

FATFS_END_PRIV_NS

class fatfs::file_allocation_table::impl
{
  public:
    explicit impl(std::string_view path);

    std::vector<file_info> read_directory(std::string_view path);
    std::vector<std::byte> read_file(std::string_view path);

    void create_file(std::string_view path, const std::vector<std::byte> &data);

    void create_directory(std::string_view path);

    [[nodiscard]] file_system_version version() const;

  private:
    std::fstream m_fs;

    // do not modify
    // {
    file_system_version m_version;

    priv::bios_parameter_block m_bpb;
    std::vector<std::byte>     m_fat;

    std::size_t m_sectors_per_fat;
    std::size_t m_total_fat_sectors;
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

    std::vector<priv::dir_entry> read_raw_directory(std::string_view path);

    void create_directory_entry(std::string_view              path,
                                const std::vector<std::byte> &data,
                                bool                          is_directory);

    [[nodiscard]] std::size_t extract_cluster(std::size_t cluster_number) const;
    void set_cluster(std::size_t cluster_number, std::size_t next);

    [[nodiscard]] std::vector<std::size_t>
    extract_cluster_chain(std::size_t start_cluster) const;

    [[nodiscard]] std::size_t get_next_free_cluster(
        std::size_t start_cluster = 1 /* start_cluster + 1 == 2*/) const;

    [[nodiscard]] std::size_t
    convert_cluster_to_sector(std::size_t cluster) const;

    [[nodiscard]] bool is_end_of_cluster_chain(std::size_t cluster) const;
};
