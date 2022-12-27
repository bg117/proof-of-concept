#pragma once

#include <stdexcept>

namespace Fatfs::Errors
{
class FileSystemError : public std::runtime_error
{
  public:
    FileSystemError(const std::string &whatArg)
        : std::runtime_error{whatArg}
    {
    }
};
class FileAlreadyExistsError : public FileSystemError
{
  public:
    FileAlreadyExistsError(const std::string &whatArg)
        : FileSystemError{whatArg}
    {
    }
};
class FileNotFoundError : public FileSystemError
{
  public:
    FileNotFoundError(const std::string &whatArg)
        : FileSystemError{whatArg}
    {
    }
};
class DirectoryNotFoundError : public FileSystemError
{
  public:
    DirectoryNotFoundError(const std::string &whatArg)
        : FileSystemError{whatArg}
    {
    }
};
class InvalidPathError : public FileSystemError
{
  public:
    InvalidPathError(const std::string &whatArg)
        : FileSystemError{whatArg}
    {
    }
};
class InvalidFileOperationError : public FileSystemError
{
  public:
    InvalidFileOperationError(const std::string &whatArg)
        : FileSystemError{whatArg}
    {
    }
};
} // namespace fatfs::errors