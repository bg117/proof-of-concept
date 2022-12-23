#include "fatfs/errors.hpp"
#include "fatfs/file_allocation_table.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

void run(const std::vector<std::string> &args);

int main(const int argc, char *argv[])
{
    const std::vector<std::string> args{argv, argv + argc};

    if (args.size() < 3)
    {
        std::cerr << "usage: " << args[0]
            << " <volume> <read|view|write> <args...>" << std::endl;
        return 1;
    }

    try
    {
        run(args);
    }
    catch (const fatfs::errors::invalid_file_operation_error &e)
    {
        std::cerr << "invalid file operation error: " << e.what() << std::endl;
        return 3;
    }
    catch (const fatfs::errors::invalid_path_error &e)
    {
        std::cerr << "invalid path error: " << e.what() << std::endl;
        return 3;
    }
    catch (const fatfs::errors::file_system_error &e)
    {
        std::cerr << "generic file system error: " << e.what() << std::endl;
        return 3;
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "error: " << e.what() << std::endl;
        return 2;
    }

    return 0;
}

void run(const std::vector<std::string> &args)
{
    const fatfs::file_allocation_table imp{args[1]};

    if (args.size() < 4)
    {
        throw std::runtime_error{"missing argument for command \"" + args[2] +
                                 "\""};
    }

    if (args[3].find('/') != std::string::npos)
    {
        throw fatfs::errors::invalid_path_error{
            "forward slash detected in file name; please use backslashes "
            "for separating directories"};
    }

    if (args[2] == "read")
    {
        std::cout << reinterpret_cast<char *>(imp.read_file(args[3]).data());
    }
    else if (args[2] == "view")
    {
        const std::vector<fatfs::file_info> entries = imp.read_directory(args[3]);

        for (const auto &[name, creation_timestamp, last_modification_timestamp,
                 last_access_date, size, is_directory] : entries)
        {
            std::cout << "Name: " << name;
            if (is_directory)
                std::cout << " (directory)";
            else
                std::cout << "\n  size: " << size << " bytes";

            std::cout << "\n  created: "
                << std::ctime(&creation_timestamp);
            std::cout << "  last modified: "
                << std::ctime(&last_modification_timestamp);
            std::cout << "  last accessed: "
                << std::ctime(&last_access_date);

            std::cout << std::endl;
        }
    }
    else if (args[2] == "write")
    {
        if (args.size() < 5)
        {
            throw std::runtime_error{"missing data for command \"" + args[2] +
                                     " " + args[3] + "\""};
        }

        // convert args[4] to vector<std::byte>
        std::vector<std::byte> data;
        data.reserve(args[4].size());

        for (const auto &c : args[4])
            data.push_back(static_cast<std::byte>(c));

        imp.write_file(args[3], data);
    }
}
