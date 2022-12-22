#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <typeinfo>

#include "poc.hpp"

void run(const std::vector<std::string> &args);

int main(int argc, char *argv[])
{
    std::vector<std::string> args{argv, argv + argc};

    if (args.size() < 3)
    {
        std::cerr << "usage: " << args[0] << " <volume> <read|view|write> <args...>"
                  << std::endl;
        return 1;
    }

    try
    {
        run(args);
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << typeid(e).name() << ": " << e.what() << std::endl;
        return 2;
    }

    return 0;
}

void run(const std::vector<std::string> &args)
{
    poc::file_allocation_table imp{args[1]};

    if (args.size() < 4)
    {
        throw std::runtime_error{"missing argument for command \"" + args[2] +
                                 "\""};
    }

    if (args[2] == "read")
    {
        std::cout << reinterpret_cast<char *>(imp.read_file(args[3]).data());
    }
    else if (args[2] == "view")
    {
        std::vector<poc::directory_entry> entries = imp.read_directory(args[3]);

        for (const auto &entry : entries)
            std::cout << "Name: " << entry.name << std::endl;
    }
    else if (args[2] == "write")
    {
        if (args.size() < 5)
        {
            throw std::runtime_error{"missing data for command \"" + args[2] +
                                     " " + args[3] + "\""};
        }

        if (args[3].find('/') != std::string::npos)
            throw std::runtime_error{"use backslash as path separator"};

        // convert args[4] to vector<std::byte>
        std::vector<std::byte> data;
        data.reserve(args[4].size());

        for (const auto &c : args[4])
            data.push_back(static_cast<std::byte>(c));

        imp.write_file(args[3], data);
    }
}
