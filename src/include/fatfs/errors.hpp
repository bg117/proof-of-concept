#pragma once

#include <stdexcept>

namespace fatfs::errors
{
class file_system_error : public std::runtime_error
{
  public:
    file_system_error(const std::string &what_arg)
        : std::runtime_error{what_arg}
    {
    }
};
class file_already_exists_error : public file_system_error
{
  public:
    file_already_exists_error(const std::string &what_arg)
        : file_system_error{what_arg}
    {
    }
};
class file_not_found_error : public file_system_error
{
  public:
    file_not_found_error(const std::string &what_arg)
        : file_system_error{what_arg}
    {
    }
};
class directory_not_found_error : public file_system_error
{
  public:
    directory_not_found_error(const std::string &what_arg)
        : file_system_error{what_arg}
    {
    }
};
class invalid_path_error : public file_system_error
{
  public:
    invalid_path_error(const std::string &what_arg)
        : file_system_error{what_arg}
    {
    }
};
class invalid_file_operation_error : public file_system_error
{
  public:
    invalid_file_operation_error(const std::string &what_arg)
        : file_system_error{what_arg}
    {
    }
};
} // namespace fatfs::errors