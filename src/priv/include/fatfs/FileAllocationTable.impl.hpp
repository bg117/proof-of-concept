#pragma once

#include "fatfs/Structures.hpp"

#include <fstream>
#include <vector>

class Fatfs::FileAllocationTable::Implementation
{
  public:
    explicit Implementation(std::string_view path);

    std::vector<FileInfo>  ReadDirectory(const std::string_view path);
    std::vector<std::byte> ReadFile(const std::string_view path);

    void CreateFile(std::string_view path, const std::vector<std::byte> &data);
    void CreateDirectory(std::string_view path);

    void DeleteEntry(std::string_view path) const;
    void EraseEntry(std::string_view path) const;

    [[nodiscard]] FileSystemVersion Version() const;

  private:
    std::fstream fstream_;

    // do not modify
    // {
    FileSystemVersion version_;

    Structures::BiosParameterBlock bpb_{};
    std::vector<std::byte>         fat_;

    std::size_t sectorsPerFat_{};

    std::size_t sectorsInDataRegion_;
    std::size_t sectorsInFatRegion_{};

    std::size_t totalDevSectors_{};
    std::size_t totalDevClusters_{};

    std::size_t firstRootDirSector_{};
    std::size_t firstDataRegionSector_{};
    std::size_t firstFatSector_{};

    std::size_t bytesPerCluster_{};

    std::size_t endOfChainIndicator_;
    // }

    std::vector<std::byte> ReadFile(const std::string_view dirEntry,
                                    const bool             isDirectory);

    std::vector<Structures::DirectoryEntry>
    ReadRawDirectory(std::string_view path);

    void CreateDirectoryEntry(std::string_view              path,
                              const std::vector<std::byte> &data,
                              bool                          isDirectory);

    [[nodiscard]] std::size_t ExtractCluster(std::size_t clusterNumber) const;
    void SetCluster(std::size_t clusterNumber, std::size_t next);

    [[nodiscard]] std::vector<std::size_t>
    ExtractClusterChain(std::size_t startCluster) const;

    [[nodiscard]] std::size_t GetNextFreeCluster(
        std::size_t startCluster = 1 /* start_cluster + 1 == 2*/) const;

    [[nodiscard]] std::size_t ConvertClusterToSector(std::size_t cluster) const;

    [[nodiscard]] bool IsEndOfClusterChain(std::size_t cluster) const;
};
