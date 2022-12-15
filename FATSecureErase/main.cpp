#include <iostream>
#include <string>

#include "fat.hpp"

int main(int argc, char* argv[])
{
	fat::driver drv{"fat.img"};
	auto root_dir = drv.read_directory(R"(\)");
	auto dir = drv.read_directory(R"(\ test)");
	auto file = drv.read_file(R"(\main.c)");

	return 0;
}
