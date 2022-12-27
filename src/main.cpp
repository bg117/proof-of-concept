#include "fatfs/Errors.hpp"
#include "fatfs/FileAllocationTable.hpp"
#include "fatfs/Structures.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

void Run(const std::vector<std::string> &args);

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
        Run(args);
    }
    catch (const Fatfs::Errors::InvalidFileOperationError &e)
    {
        std::cerr << "invalid file operation error: " << e.what() << std::endl;
        return 3;
    }
    catch (const Fatfs::Errors::InvalidPathError &e)
    {
        std::cerr << "invalid path error: " << e.what() << std::endl;
        return 3;
    }
    catch (const Fatfs::Errors::FileSystemError &e)
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

void Run(const std::vector<std::string> &args)
{
    const Fatfs::FileAllocationTable imp{args[1]};

    if (args.size() < 4)
    {
        throw std::runtime_error{"missing argument for command \"" + args[2] +
                                 "\""};
    }

    if (args[3].find('/') != std::string::npos)
    {
        throw Fatfs::Errors::InvalidPathError{
            "forward slash detected in file name; please use backslashes "
            "for separating directories"};
    }

    if (args[2] == "read")
    {
        std::cout << reinterpret_cast<char *>(imp.ReadFile(args[3]).data());
    }
    else if (args[2] == "view")
    {
        const std::vector<Fatfs::FileInfo> entries = imp.ReadDirectory(args[3]);

        for (const auto &[name,
                          creationTimestamp,
                          lastModificationTimestamp,
                          lastAccessDate, size,
                          isDirectory] : entries)
        {
            std::cout << "Name: " << name;
            if (isDirectory)
                std::cout << " (directory)";
            else
                std::cout << "\n  size: " << size << " bytes";

            std::cout << "\n  created: "
                << std::ctime(&creationTimestamp);
            std::cout << "  last modified: "
                << std::ctime(&lastModificationTimestamp);
            std::cout << "  last accessed: "
                << std::ctime(&lastAccessDate);

            std::cout << std::endl;
        }
    }
    else if (args[2] == "create")
    {
        if (args.size() < 5)
        {
            throw std::runtime_error{"missing data for command \"" + args[2] +
                                     " " + args[3] + "\""};
        }

        // if args[3] is "-d", create a directory
        if (args[3] == "-d")
        {
            imp.CreateDirectory(args[4]);
            return;
        }

        // convert args[4] to vector<std::byte>
        std::vector<std::byte> data;
        data.reserve(args[4].size());

        for (const auto &c : args[4])
            data.push_back(static_cast<std::byte>(c));

        imp.CreateFile(args[3], data);
    }
}
