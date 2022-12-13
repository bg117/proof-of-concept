#include "fat.hpp"

/*
int fat_read_file(const struct fat_open_context* ctx, const struct fat_directory_entry* parent,
                  const size_t parent_entries, const char* name, const char* ext, const int attribute, void* buffer)
{
	for (
		const struct fat_directory_entry* q = parent; q < parent + parent_entries; q++)
	{
		if (!(_strnicmp(name, q->name, 8) == 0 &&
			_strnicmp(ext, q->extension, 3) == 0 &&
			attribute == q->attributes))
			continue;

		const size_t sectors_per_fat = ctx->bpb.offset_36.fat32.sectors_per_fat_32;
		const size_t start_of_data_region = ctx->bpb.reserved_sectors + ctx->bpb.number_of_fats * sectors_per_fat + ctx
			->bpb.root_dir_entries * sizeof(struct fat_directory_entry) / ctx->bpb.bytes_per_sector;
		const size_t bytes_per_cluster = ctx->bpb.sectors_per_cluster * ctx->bpb.bytes_per_sector;

		// allocate memory for the FAT
		uint32_t* fat = malloc(ctx->bpb.number_of_fats * sectors_per_fat * ctx->bpb.bytes_per_sector);
		if (fat_read_fat(ctx, fat))
			return 1;

		// get root directory first cluster index
		uint32_t index = q->first_cluster_low | q->first_cluster_high << 16;
		size_t size = 0; // for when buffer is NULL
		uint8_t* p = buffer;

		do
		{
			if (p == nullptr)
			{
				size += bytes_per_cluster;
			}
			else
			{
				// seek to position of cluster in disk
				if (fseek(
					ctx->fp,
					(start_of_data_region + (index - 2) * ctx->bpb.sectors_per_cluster) * ctx->bpb.bytes_per_sector,
					SEEK_SET))
					return 2;

				// read cluster
				if (fread(p, 1, bytes_per_cluster, ctx->fp) != bytes_per_cluster)
					return 3;

				// advance pointer by number of bytes read
				p += bytes_per_cluster;
			}

			// get next cluster index
			index = fat[index];
		}
		while (index < 0x0FFFFFF7);
		// indices greater than 0x0FFFFFF6 are either damaged or indicates the last cluster in the chain

		free(fat);

		if (p == nullptr)
			return size; // return size of file
	}

	return 0;
}

*/

fat::driver::driver(std::string_view filename) : m_bpb()
{
	std::ifstream ifs{filename.data(), std::ios::binary};
	m_ifs = std::move(ifs);

	// copy BPB to struct
	m_ifs.seekg(0);
	m_ifs.read(reinterpret_cast<char*>(&m_bpb), sizeof m_bpb);
}

std::vector<fat::directory_entry> fat::driver::read_root_directory()
{
	size_t sectors_per_fat = m_bpb.sectors_per_fat_16;

	if (type() != type::fat32)
	{
		const size_t start_of_root_directory = m_bpb.reserved_sectors + m_bpb.number_of_fats * sectors_per_fat;

		// root directory in FAT12 and FAT16 has a fixed size and is located at a fixed offset (directly after the FAT table)
		std::vector<directory_entry> root_directory(sizeof(directory_entry) * m_bpb.root_dir_entries);

		m_ifs.seekg(start_of_root_directory * m_bpb.bytes_per_sector, std::ios::beg);
		m_ifs.read(reinterpret_cast<char*>(root_directory.data()), sizeof(directory_entry) * m_bpb.root_dir_entries);

		return root_directory;
	}

	// if volume is FAT32 then the sectors_per_fat_16 field is zero and the actual number of sectors per FAT is stored in the FAT32 BPB extension
	sectors_per_fat = m_bpb.offset_36.fat32.sectors_per_fat_32;

	// some constants
	const size_t entries_per_cluster = m_bpb.sectors_per_cluster * m_bpb.bytes_per_sector / sizeof(directory_entry);
	const size_t start_of_root_directory = m_bpb.reserved_sectors + m_bpb.number_of_fats * sectors_per_fat;

	// obtain FAT table
	auto tmp = read_fat();
	const auto ptr = reinterpret_cast<uint32_t*>(tmp.data());
	const auto fat = std::vector(ptr, ptr + tmp.size() / sizeof(uint32_t));

	std::vector<directory_entry> root_directory{};

	// get root directory first cluster index
	size_t index = m_bpb.offset_36.fat32.root_cluster;
	size_t reserve = 0;

	do
	{
		// seek to position of cluster in disk
		m_ifs.seekg((start_of_root_directory + (index - 2) * m_bpb.sectors_per_cluster) * m_bpb.bytes_per_sector,
		            std::ios::beg);

		root_directory.resize(reserve += entries_per_cluster);

		// read cluster
		m_ifs.read(reinterpret_cast<char*>(root_directory.data() + reserve - entries_per_cluster),
		           sizeof(directory_entry) * entries_per_cluster);

		// get next cluster index
		index = fat[index];
	}
	while (index < 0x0FFFFFF7);
	// indices greater than 0x0FFFFFF6 are either damaged or indicates the last cluster in the chain

	// remove null entries
	root_directory.erase(std::remove_if(root_directory.begin(), root_directory.end(), [](const directory_entry& x)
	{
		return x.name[0] == '\0' && x.extension[0] == '\0';
	}), root_directory.end());
	root_directory.shrink_to_fit();

	return root_directory;
}

std::vector<uint8_t> fat::driver::read_fat()
{
	const size_t sectors_per_fat = m_bpb.sectors_per_fat_16 == 0
		                               ? m_bpb.offset_36.fat32.sectors_per_fat_32
		                               : m_bpb.sectors_per_fat_16;
	const size_t fat_bytes = sectors_per_fat * m_bpb.number_of_fats * m_bpb.bytes_per_sector;

	std::vector<uint8_t> fat(fat_bytes);

	m_ifs.seekg(m_bpb.reserved_sectors * m_bpb.bytes_per_sector, std::ios::beg);
	m_ifs.read(reinterpret_cast<char*>(fat.data()), fat_bytes);

	return fat;
}

fat::type fat::driver::type() const
{
	const size_t sectors_per_fat = m_bpb.sectors_per_fat_16 == 0
		                               ? m_bpb.offset_36.fat32.sectors_per_fat_32
		                               : m_bpb.sectors_per_fat_16;
	const size_t total_sectors = m_bpb.total_sectors_16 == 0
		                             ? m_bpb.total_sectors_32
		                             : m_bpb.total_sectors_16;

	const size_t start_of_root_directory = m_bpb.reserved_sectors + m_bpb.number_of_fats * sectors_per_fat;
	const size_t start_of_data_region = start_of_root_directory + m_bpb.root_dir_entries
		/* in FAT32, the root directory is part of the data region */ * sizeof(directory_entry) / m_bpb.
		bytes_per_sector;
	const size_t data_region_size = total_sectors - start_of_data_region;

	const size_t total_clusters = data_region_size / m_bpb.sectors_per_cluster;

	if (total_clusters < 4085)
		return type::fat12;

	if (total_clusters < 65525)
		return type::fat16;

	return type::fat32;
}
