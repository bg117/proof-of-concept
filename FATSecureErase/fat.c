#include "fat.h"

#include <errno.h>
#include <stdlib.h>

// opaque struct definition
struct fat_open_context
{
	FILE* fp;
	struct fat_bpb bpb;
};

struct fat_open_context* fat_open(const char* filename)
{
	FILE* fp;
	if (fopen_s(&fp, filename, "rb"))
		return NULL;

	struct fat_open_context* ctx = malloc(sizeof *ctx);
	ctx->fp = fp;

	if (fseek(ctx->fp, 0, SEEK_SET))
	{
		errno = 1;
		return NULL;
	}

	if (fread(&ctx->bpb, 1, sizeof ctx->bpb, fp) != sizeof ctx->bpb)
	{
		errno = 2;
		return NULL;
	}

	return ctx;
}

int fat_close(struct fat_open_context* ctx)
{
	if (fclose(ctx->fp) != 0)
		return 1;

	free(ctx);

	return 0;
}

int fat_read_root_directory(const struct fat_open_context* ctx, struct fat_directory_entry* root_directory)
{
	uint32_t sectors_per_fat = ctx->bpb.sectors_per_fat_16;

	if (fat_determine_fat_type(ctx) != FAT32)
	{
		if (root_directory != NULL)
			return ctx->bpb.root_dir_entries;

		const uint32_t start_of_root_directory = ctx->bpb.reserved_sectors + ctx->bpb.number_of_fats * sectors_per_fat;

		// root directory in FAT12 and FAT16 has a fixed size and is located at a fixed offset (directly after the FAT table)
		if (fseek(ctx->fp, start_of_root_directory * ctx->bpb.bytes_per_sector, SEEK_SET))
			return 1;

		// read the root directory
		if (fread(root_directory, 1, sizeof *root_directory * ctx->bpb.root_dir_entries, ctx->fp) != sizeof *
			root_directory * ctx->bpb.
			                      root_dir_entries)
			return 2;
	}
	else
	{
		// if volume is FAT32 then the sectors_per_fat_16 field is zero and the actual number of sectors per FAT is stored in the FAT32 BPB extension
		sectors_per_fat = ctx->bpb.offset_36.fat32.sectors_per_fat_32;

		// some constants
		const int entries_per_cluster = ctx->bpb.sectors_per_cluster * ctx->bpb.bytes_per_sector / sizeof *
			root_directory;
		const int start_of_root_directory = ctx->bpb.reserved_sectors + ctx->bpb.number_of_fats * sectors_per_fat;

		// don't modify the root_directory parameter
		struct fat_directory_entry* p = root_directory;

		// allocate memory for the FAT
		uint32_t* fat = malloc(ctx->bpb.number_of_fats * sectors_per_fat * ctx->bpb.bytes_per_sector);
		if (fat_read_fat(ctx, fat))
			return 3;

		// get root directory first cluster index
		uint32_t index = ctx->bpb.offset_36.fat32.root_cluster;
		int i = 0; // for when root_directory is NULL

		do
		{
			if (root_directory == NULL)
			{
				i++;
			}
			else
			{
				// seek to position of cluster in disk
				if (fseek(
					ctx->fp,
					(start_of_root_directory + (index - 2) * ctx->bpb.sectors_per_cluster) * ctx->bpb.bytes_per_sector,
					SEEK_SET))
					return 4;

				// read cluster
				if (fread(p, 1, sizeof *root_directory * entries_per_cluster, ctx->fp) != sizeof *root_directory *
					entries_per_cluster)
					return 5;

				// advance pointer by number of entries read
				p += entries_per_cluster;
			}

			// get next cluster index
			index = fat[index];
		}
		while (index < 0x0FFFFFF7);
		// indices greater than 0x0FFFFFF6 are either damaged or indicates the last cluster in the chain

		free(fat); // free memory allocated for the FAT

		if (root_directory == NULL)
			return i * entries_per_cluster; // return number of entries in root directory
	}

	return 0;
}

int fat_read_fat(const struct fat_open_context* ctx, void* fat)
{
	const uint32_t sectors_per_fat = ctx->bpb.sectors_per_fat_16 == 0
		                                 ? ctx->bpb.offset_36.fat32.sectors_per_fat_32
		                                 : ctx->bpb.sectors_per_fat_16;
	const uint32_t fat_bytes = sectors_per_fat * ctx->bpb.number_of_fats * ctx->bpb.bytes_per_sector;

	if (fseek(ctx->fp, ctx->bpb.reserved_sectors * ctx->bpb.bytes_per_sector, SEEK_SET))
		return 1;

	if (fread(fat, 1, fat_bytes, ctx->fp) != fat_bytes)
		return 2;

	return 0;
}

int fat_determine_fat_type(const struct fat_open_context* ctx)
{
	const uint32_t sectors_per_fat = ctx->bpb.sectors_per_fat_16 == 0
		                                 ? ctx->bpb.offset_36.fat32.sectors_per_fat_32
		                                 : ctx->bpb.sectors_per_fat_16;
	const uint32_t total_sectors = ctx->bpb.total_sectors_16 == 0
		                               ? ctx->bpb.total_sectors_32
		                               : ctx->bpb.total_sectors_16;

	const uint32_t start_of_root_directory = ctx->bpb.reserved_sectors + ctx->bpb.number_of_fats * sectors_per_fat;
	const uint32_t start_of_data_region = start_of_root_directory + ctx->bpb.root_dir_entries
		/* in FAT32, the root directory is part of the data region */ * sizeof(struct fat_directory_entry) / ctx->bpb.
		bytes_per_sector;
	const uint32_t data_region_size = total_sectors - start_of_data_region;

	const uint32_t total_clusters = data_region_size / ctx->bpb.sectors_per_cluster;

	if (total_clusters < 4085)
		return FAT12;

	if (total_clusters < 65525)
		return FAT16;

	return FAT32;
}
