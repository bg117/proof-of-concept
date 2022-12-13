#pragma once

#include <cstdint>
#include <vector>
#include <fstream>

namespace fat
{
#pragma region POD structures

#pragma pack(push, 1)

	struct fat12_16_ebr
	{
		uint8_t drive_number;
		uint8_t reserved;
		uint8_t boot_signature;
		uint32_t volume_id;
		char volume_label[11];
		char file_system_type[8];
	};

	struct fat32_ebr
	{
		uint32_t sectors_per_fat_32;
		uint16_t extended_flags;
		uint16_t fs_version;
		uint32_t root_cluster;
		uint16_t fs_info;
		uint16_t backup_boot_sector;
		uint8_t zero[12];
		uint8_t drive_number;
		uint8_t reserved;
		uint8_t boot_signature;
		uint32_t volume_id;
		char volume_label[11];
		char file_system_type[8];
	};

	union struct_at_offset_36
	{
		fat12_16_ebr fat12_16;
		fat32_ebr fat32;
	};

	struct bpb
	{
		uint8_t jmp[3];
		char oem_name[8];
		uint16_t bytes_per_sector;
		uint8_t sectors_per_cluster;
		uint16_t reserved_sectors;
		uint8_t number_of_fats;
		uint16_t root_dir_entries;
		uint16_t total_sectors_16;
		uint8_t media_descriptor;
		uint16_t sectors_per_fat_16;
		uint16_t sectors_per_track;
		uint16_t number_of_heads;
		uint32_t number_of_hidden_sectors;
		uint32_t total_sectors_32;

		struct_at_offset_36 offset_36;
	};

	struct directory_entry
	{
		char name[8];
		char extension[3];
		uint8_t attributes;
		uint8_t reserved;
		uint8_t creation_time_tenths;
		uint16_t creation_time;
		uint16_t creation_date;
		uint16_t last_access_date;
		uint16_t first_cluster_high;
		uint16_t last_modification_time;
		uint16_t last_modification_date;
		uint16_t first_cluster_low;
		uint32_t file_size;
	};

#pragma pack(pop)
	
#pragma endregion

	enum class type
	{
		fat12,
		fat16,
		fat32
	};

	class driver
	{
	public:
		explicit driver(std::string_view filename);
		explicit driver(std::istream&& is);
		explicit driver(std::ifstream&& ifs);

		std::vector<directory_entry> read_root_directory();
		std::vector<uint8_t> read_fat();
		type type() const;

	private:
		std::ifstream m_ifs;
		bpb m_bpb;
	};
}
