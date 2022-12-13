#include <iostream>
#include <string>

#include "fat.hpp"

int main(int argc, char* argv[])
{
	fat::driver drv{ "fat.img" };
	auto root_dir = drv.read_root_directory();

	return 0;
}
