#include <stdlib.h>

#include "fat.h"

int main(int argc, char* argv[])
{
	struct fat_open_context* ctx = fat_open("fat.img");
	if (ctx == NULL)
		return 2;

	const int length = fat_read_root_directory(ctx, NULL);
	struct fat_directory_entry* root_directory = malloc(length * sizeof *root_directory);
	if (fat_read_root_directory(ctx, root_directory) != 0)
		return 3;

	free(root_directory);

	fat_close(ctx);

	return 0;
}
