#pragma once

#include <bits/stdint-uintn.h>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <vector>

namespace poc
{
#pragma region POD structures

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

struct directory_entry
{
    struct date
    {
        uint8_t day   : 5;
        uint8_t month : 4;
        uint8_t year  : 7; // since 1980
    };

    struct time
    {
        uint8_t second : 5;
        uint8_t minute : 6;
        uint8_t hour   : 5;
    };

    uint8_t  name[8];
    uint8_t  extension[3];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  creation_time_tenths;
    time     creation_time;
    date     creation_date;
    date     last_access_date;
    uint16_t first_cluster_high;
    time     last_modification_time;
    date     last_modification_date;
    uint16_t first_cluster_low;
    uint32_t file_size;

    enum class attribute
    {
        read_only = 0x01,
        hidden    = 0x02,
        system    = 0x04,
        volume_id = 0x08,
        directory = 0x10,
        archive   = 0x20,
        long_name = read_only | hidden | system | volume_id
    };
};

#pragma pack(pop)

#pragma endregion

class file_allocation_table
{
    using binary_type    = std::vector<std::byte>;
    using directory_type = std::vector<directory_entry>;

  public:
    enum class version
    {
        fat12,
        fat16,
        fat32
    };

    explicit file_allocation_table(std::string_view path);

    directory_type read_directory(std::string_view path);
    binary_type    read_file(std::string_view path);

    void write_file(std::string_view path, binary_type data);

    version file_system_version() const;

  private:
    std::fstream         m_fs;
    bios_parameter_block m_bpb;

    binary_type read_fat();

    directory_type read_root_directory();

    binary_type read_file_internal(std::string_view path, bool is_directory);

    uint32_t get_next_free_cluster(const binary_type &fat, uint32_t start_cluster = 1 /* start_cluster + 1 == 2*/);
};

class miscellaneous
{
  public:
    static std::string convert_normal_to_8_3(std::string_view name);
    static std::string convert_8_3_to_normal(std::string_view name);
};
} // namespace poc
