#include "fat.hpp"

#include <algorithm>
#include <sstream>
#include <cstring>
#include <unordered_set>

namespace
{
	std::string convert_normal_to_8_3(const std::string &name);
	std::string convert_8_3_to_normal(const std::string &name);
	std::string trim_string(const std::string &str);
}

poc::file_allocation_table::file_allocation_table(std::string_view path) : m_bpb()
{
	std::ifstream ifs{path.data(), std::ios::binary};
	m_ifs = std::move(ifs);

	if (!m_ifs.is_open())
		throw std::runtime_error{"failed to open file " + std::string{path}};

	// copy BPB to struct
	m_ifs.seekg(0);
	m_ifs.read(reinterpret_cast<char *>(&m_bpb), sizeof m_bpb);
}

std::vector<poc::directory_entry> poc::file_allocation_table::read_root_directory()
{
	std::size_t sectors_per_fat = m_bpb.sectors_per_fat_16;

	if (file_system_version() != version::fat32)
	{
		const std::size_t start_of_root_directory = m_bpb.reserved_sectors + m_bpb.number_of_fats * sectors_per_fat;

		// root directory in FAT12 and FAT16 has a fixed size and is located at a fixed offset (directly after the FAT table)
		directory_type root_directory(sizeof(directory_entry) * m_bpb.root_dir_entries);

		m_ifs.seekg(start_of_root_directory * m_bpb.bytes_per_sector, std::ios::beg);
		m_ifs.read(reinterpret_cast<char *>(root_directory.data()), sizeof(directory_entry) * m_bpb.root_dir_entries);

		return root_directory;
	}

	// if volume is FAT32 then the sectors_per_fat_16 field is zero and the actual number of sectors per FAT is stored in the FAT32 BPB extension
	sectors_per_fat = m_bpb.offset_36.fat32.sectors_per_fat_32;

	// some constants
	const std::size_t entries_per_cluster = m_bpb.sectors_per_cluster * m_bpb.bytes_per_sector / sizeof(directory_entry);
	const std::size_t start_of_root_directory = m_bpb.reserved_sectors + m_bpb.number_of_fats * sectors_per_fat;

	// obtain FAT table
	binary_type fat = read_fat();
	const auto ptr32 = reinterpret_cast<std::uint32_t *>(fat.data());

	directory_type root_directory{};

	// get root directory first cluster index
	std::size_t index = m_bpb.offset_36.fat32.root_cluster;
	std::size_t reserve = 0;

	do
	{
		// seek to position of cluster in disk
		m_ifs.seekg((start_of_root_directory + (index - 2) * m_bpb.sectors_per_cluster) * m_bpb.bytes_per_sector,
					std::ios::beg);

		root_directory.resize(reserve += entries_per_cluster);

		// read cluster
		m_ifs.read(reinterpret_cast<char *>(root_directory.data() + reserve - entries_per_cluster),
				   sizeof(directory_entry) * entries_per_cluster);

		// get next cluster index
		index = ptr32[index];
	} while (index < 0x0FFFFFF7);
	// indices greater than 0x0FFFFFF6 are either damaged or indicates the last cluster in the chain

	// remove null entries
	root_directory.erase(std::remove_if(root_directory.begin(), root_directory.end(), [](const directory_entry &x)
										{ return x.name[0] == '\0' && x.extension[0] == '\0'; }),
						 root_directory.end());
	root_directory.shrink_to_fit();

	return root_directory;
}

poc::file_allocation_table::binary_type poc::file_allocation_table::read_fat()
{
	const std::size_t sectors_per_fat = m_bpb.sectors_per_fat_16 == 0
											? m_bpb.offset_36.fat32.sectors_per_fat_32
											: m_bpb.sectors_per_fat_16;
	const std::size_t fat_bytes = sectors_per_fat * m_bpb.number_of_fats * m_bpb.bytes_per_sector;

	binary_type fat(fat_bytes);

	m_ifs.seekg(m_bpb.reserved_sectors * m_bpb.bytes_per_sector, std::ios::beg);
	m_ifs.read(reinterpret_cast<char *>(fat.data()), fat_bytes);

	return fat;
}

std::vector<poc::directory_entry> poc::file_allocation_table::read_directory(const std::string_view path)
{
	// if root path is given then return root directory
	if (trim_string(path.data()) == "\\")
		return read_root_directory();

	binary_type contents = read_file_internal(path, true);

	const std::byte *ptr = contents.data();
	const std::size_t len = contents.size() / sizeof(directory_entry);
	const auto arr = reinterpret_cast<const directory_entry *>(ptr);

	std::vector dir(arr, arr + len);

	// remove null entries
	dir.erase(std::remove_if(dir.begin(), dir.end(), [](const directory_entry &x)
							 { return x.name[0] == '\0' && x.extension[0] == '\0'; }),
			  dir.end());
	dir.shrink_to_fit();

	return dir;
}

poc::file_allocation_table::binary_type poc::file_allocation_table::read_file(const std::string_view path)
{
	return read_file_internal(path, false);
}

poc::file_allocation_table::binary_type poc::file_allocation_table::read_file_internal(const std::string_view path, bool is_directory)
{
	if (trim_string(path.data()).empty())
		throw std::runtime_error{"path is empty"};

	const std::string lpath{path};
	std::string tmp;

	std::stringstream ss{lpath};
	std::vector<std::string> path_components{};

	// split on backslash
	while (std::getline(ss, tmp, '\\'))
		path_components.emplace_back(convert_normal_to_8_3(trim_string(tmp)));

	path_components.erase(std::remove_if(path_components.begin(), path_components.end(), [](const std::string_view x)
										 { return trim_string(x.data()).empty(); }),
						  path_components.end());
	path_components.shrink_to_fit();

	binary_type contents{};

	directory_type parent = read_root_directory();

	for (std::size_t i = 0; i < path_components.size(); ++i)
	{
		auto &x = path_components[i];

		// find directory entry
		auto entry = std::find_if(parent.begin(), parent.end(),
								  [&i, &path_components, &x, &is_directory](const directory_entry &y)
								  {
									  const bool v1 = std::strncmp(x.data(), y.name, 8) == 0;
									  const bool v2 = std::strncmp(x.data() + 8, y.extension, 3) == 0;

									  // if there are more path components then this one must be a directory
									  // otherwise, if is_directory is true then this one must be a directory, else a file
									  return v1 && v2;
								  });

		if (entry == parent.end())
			throw std::runtime_error{"file/directory '" + convert_8_3_to_normal(x) + "' not found"};

		// if entry is file and there are more path components then throw exception
		if (i != path_components.size() - 1 && !(entry->attributes & static_cast<int>(
																		 directory_entry::attribute::directory)))
		{
			throw std::runtime_error{
				"file '" + convert_8_3_to_normal(x) + "' is not a directory, trying to browse contents of it"};
		}

		const std::size_t sectors_per_fat = m_bpb.sectors_per_fat_16 == 0
												? m_bpb.offset_36.fat32.sectors_per_fat_32
												: m_bpb.sectors_per_fat_16;
		const std::size_t start_of_root_directory = m_bpb.reserved_sectors + m_bpb.number_of_fats * sectors_per_fat;
		const std::size_t start_of_data_region = start_of_root_directory + m_bpb.root_dir_entries;
		const std::size_t bytes_per_cluster = m_bpb.sectors_per_cluster * m_bpb.bytes_per_sector;

		const version fsver = file_system_version();

		binary_type fat = read_fat();

		std::size_t cluster = entry->first_cluster_low | entry->first_cluster_high << 16;
		std::size_t reserve = 0;

		do
		{
			// seek to position of cluster in disk
			m_ifs.seekg((start_of_data_region + (cluster - 2) * m_bpb.sectors_per_cluster) * m_bpb.bytes_per_sector,
						std::ios::beg);

			contents.resize(reserve += bytes_per_cluster);

			// read cluster
			m_ifs.read(reinterpret_cast<char *>(contents.data() + reserve - bytes_per_cluster),
					   bytes_per_cluster);

			if (fsver == version::fat12)
			{
				auto orig = cluster;

				cluster = cluster * 3 / 2;
				cluster = *reinterpret_cast<std::uint16_t *>(reinterpret_cast<std::uint8_t *>(fat.data()) + cluster); // basically, get 2 bytes from location of FAT + cluster (no pointer arithmetic)

				if (orig % 2 == 0)
					cluster &= 0xFFF;
				else
					cluster >>= 4;
			}
			else if (fsver == version::fat16)
			{
				cluster = reinterpret_cast<std::uint16_t *>(fat.data())[cluster];
			}
			else
			{
				cluster = reinterpret_cast<std::uint32_t *>(fat.data())[cluster];
			}
		} while ((fsver == version::fat12 && cluster < 0xFF7) || (fsver == version::fat16 && cluster < 0xFFF7) || (fsver == version::fat32 && cluster < 0x0FFFFFF7));

		// resize contents to actual size
		contents = {contents.data(), contents.data() + entry->file_size};
	}

	return contents;
}

poc::file_allocation_table::version poc::file_allocation_table::file_system_version() const
{
	const std::size_t sectors_per_fat = m_bpb.sectors_per_fat_16 == 0
											? m_bpb.offset_36.fat32.sectors_per_fat_32
											: m_bpb.sectors_per_fat_16;
	const std::size_t total_sectors = m_bpb.total_sectors_16 == 0
										  ? m_bpb.total_sectors_32
										  : m_bpb.total_sectors_16;

	const std::size_t start_of_root_directory = m_bpb.reserved_sectors + m_bpb.number_of_fats * sectors_per_fat;
	const std::size_t start_of_data_region = start_of_root_directory + m_bpb.root_dir_entries
																		   /* in FAT32, the root directory is part of the data region */
																		   * sizeof(directory_entry) / m_bpb.bytes_per_sector;
	const std::size_t data_region_size = total_sectors - start_of_data_region;

	const std::size_t total_clusters = data_region_size / m_bpb.sectors_per_cluster;

	if (total_clusters < 4085)
		return version::fat12;

	if (total_clusters < 65525)
		return version::fat16;

	return version::fat32;
}

std::uint32_t poc::file_allocation_table::get_first_missing_cluster()
{
	std::unordered_set<std::uint32_t> clusters{};
	binary_type fat = read_fat();
	const version fsver = file_system_version();
	const std::uint32_t root_cluster = m_bpb.offset_36.fat32.root_cluster;

	// add clusters that are used already
	if (fsver == version::fat12)
	{
		// if FAT12, do the weird stuff
		for (std::size_t i = 0; i < fat.size() / 3 * 2; ++i)
		{
			auto orig = i;

			std::uint32_t cluster = i * 3 / 2;
			cluster = *reinterpret_cast<std::uint16_t *>(reinterpret_cast<std::uint8_t *>(fat.data()) + cluster);

			if (orig % 2 == 0)
				cluster &= 0xFFF;
			else
				cluster >>= 4;

			if (cluster != 0 && cluster < 0xFF7)
				clusters.insert(i);
		}
	}
	// FAT16 and FAT32 are very straightforward
	else if (fsver == version::fat16)
	{
		auto tmp = reinterpret_cast<std::uint16_t *>(fat.data());
		for (std::size_t i = 0; i < fat.size() / sizeof *tmp; ++i)
		{
			std::uint32_t cluster = tmp[i];
			if (cluster != 0 && cluster < 0xFFF7)
				clusters.insert(i);
		}
	}
	else
	{
		// also insert root directory first cluster
		clusters.insert(root_cluster);

		auto tmp = reinterpret_cast<std::uint32_t *>(fat.data());
		for (std::size_t i = 0; i < fat.size() / sizeof *tmp; ++i)
		{
			std::uint32_t cluster = tmp[i];
			if (cluster != 0 && cluster < 0x0FFFFFF7)
				clusters.insert(i);
		}
	}

	// find the first missing cluster
	for (std::uint32_t i = 2; i < std::numeric_limits<std::uint32_t>::max(); ++i)
	{
		if (clusters.find(i) == clusters.end())
			return i;
	}

	return 0; // 0 is an invalid cluster
}

namespace
{
	std::string convert_normal_to_8_3(const std::string &name)
	{
		std::string result;
		result.reserve(11);

		auto it = name.begin();
		const auto end = name.end();

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

	std::string convert_8_3_to_normal(const std::string &name)
	{
		std::string r = name;
		// if r is not 11 characters long, resize and optionally pad with spaces
		if (r.size() != 11)
			r.resize(11, ' ');

		std::string result;
		result.reserve(11);

		auto it = r.begin();
		const auto end = r.end();

		// copy the first 8 characters
		for (int i = 0; i < 8 && it != end; ++i, ++it)
			result += *it;

		// remove trailing spaces
		result.erase(std::find_if(result.rbegin(), result.rend(), [](const char c)
								  { return c != ' '; })
						 .base(),
					 result.end());

		// if there is an extension, add a dot
		if (*it != ' ' && *(it + 1) != ' ' && *(it + 2) != ' ')
			result += '.';

		// copy the extension
		for (int i = 0; i < 3 && it != end; ++i, ++it)
			result += *it;

		// remove trailing spaces
		result.erase(std::find_if(result.rbegin(), result.rend(), [](const char c)
								  { return c != ' '; })
						 .base(),
					 result.end());

		return result;
	}

	std::string trim_string(const std::string &str)
	{
		std::string result = str;
		result.erase(std::find_if(result.rbegin(), result.rend(), [](const char c)
								  { return c != ' '; })
						 .base(),
					 result.end());
		return result;
	}
}
