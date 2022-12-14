#include <iostream>
#include <string>

#include "fat.hpp"

int main(int argc, char* argv[])
{
	fat::driver drv{"fat.img"};
	auto root_dir = drv.read_root_directory();
	auto dir = drv.read_file<true>("test");
	auto file = drv.read_file("main.c");

	return 0;
}
