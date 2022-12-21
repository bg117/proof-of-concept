#include <iostream>
#include <iterator>
#include <string>

#include "poc.hpp"

int main(int argc, char *argv[])
{
    std::vector<std::string> args{argv, argv + argc};
    std::byte                sample_data[768];

    // randomly fill sample data
    for (int i = 0; i < std::size(sample_data); i++)
        sample_data[i] = static_cast<std::byte>(rand() % 256);

    try
    {
        poc::file_allocation_table imp{argv[1]};
        imp.write_file("\\TEST\\bro.txt",
                       std::vector(sample_data, sample_data + std::size(sample_data)));

        if (args[2] == "-r")
        {
            std::cout << reinterpret_cast<char *>(imp.read_file(args[3]).data())
                      << std::endl;
        }
        else if (args[2] == "-d")
        {
            std::vector<poc::directory_entry> entries =
                imp.read_directory(args[3]);

            for (const auto &entry : entries)
                std::cout << "Name: " << entry.name << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
