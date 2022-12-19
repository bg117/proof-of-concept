#include <iostream>
#include <string>

#include "fat.hpp"

int main(int argc, char *argv[])
{
	std::vector<std::string> args{argv, argv + argc};

	try
	{
		poc::file_allocation_table imp{argv[1]};

		if (args[2] == "-r")
			std::cout << reinterpret_cast<char *>(imp.read_file(args[3]).data()) << std::endl;
	}
	catch (const std::exception &e)
	{
		std::cerr << "error: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}
