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

	enum class directory_entry_attribute
	{
		read_only = 0x01,
		hidden = 0x02,
		system = 0x04,
		volume_id = 0x08,
		directory = 0x10,
		archive = 0x20,
		long_name = read_only | hidden | system | volume_id
	};

	class driver
	{
	public:
		explicit driver(std::string_view path);

		std::vector<directory_entry> read_root_directory();
		std::vector<std::byte> read_fat();

		template <bool IsDirectory, std::enable_if_t<IsDirectory>* = nullptr>
		std::vector<directory_entry> read_file(std::string_view path)
		{
			auto contents = read_file_internal(path, true);

			const auto ptr = contents.data();
			const auto len = contents.size() / sizeof(directory_entry);
			const auto arr = reinterpret_cast<directory_entry*>(ptr);

			std::vector dir(arr, arr + len);

			// remove null entries
			dir.erase(std::remove_if(dir.begin(), dir.end(), [](const directory_entry& x)
			{
				return x.name[0] == '\0' && x.extension[0] == '\0';
			}), dir.end());
			dir.shrink_to_fit();

			return dir;
		}

		template <bool IsDirectory = false, std::enable_if_t<!IsDirectory>* = nullptr>
		std::basic_string<std::byte> read_file(std::string_view path)
		{
			return read_file_internal(path, false);
		}

		type type() const;

	private:
		std::ifstream m_ifs;
		bpb m_bpb;

		std::basic_string<std::byte> read_file_internal(std::string_view path, bool is_directory);
	};
}
