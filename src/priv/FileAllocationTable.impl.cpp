#include "fatfs/FileAllocationTable.hpp"
#include "fatfs/Errors.hpp"
#include "fatfs/FileAllocationTable.impl.hpp"
#include "fatfs/Helpers.hpp"
#include "fatfs/Structures.hpp"

#include "utilities/String.hpp"

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <sstream>
#include <string_view>
#include <vector>

namespace
{

template<typename T>
concept Integral = std::is_integral_v<T> || std::is_enum_v<T>;

template<Integral T, Integral U = T>
constexpr auto RoundUp(const T num, const U multiple)
{
    return (num + multiple - 1) / multiple * multiple;
}

template<Integral T, Integral U = T>
constexpr bool IsBitSet(const T seq, const U bit)
{
    return (seq & bit) == bit;
}

} // namespace

Fatfs::FileAllocationTable::Implementation::Implementation(
    const std::string_view path)
    : bpb_()
{
    fstream_.open(path.data(), std::ios::binary | std::ios::in | std::ios::out);
    if (!fstream_.is_open())
        throw std::runtime_error{"failed to open file " + std::string{path}};

    // copy BPB to struct
    fstream_.seekg(0);
    fstream_.read(reinterpret_cast<char *>(&bpb_), sizeof bpb_);

    // fill in missing fields
    sectorsPerFat_ = bpb_.SectorsPerFat == 0 ? bpb_.Offset36.Fat32.SectorsPerFat
                                             : bpb_.SectorsPerFat;
    sectorsInFatRegion_ = sectorsPerFat_ * bpb_.NumberOfFats;
    totalDevSectors_ =
        bpb_.TotalSectors == 0 ? bpb_.TotalSectorsLarge : bpb_.TotalSectors;

    firstFatSector_     = bpb_.ReservedSectors;
    firstRootDirSector_ = firstFatSector_ + bpb_.NumberOfFats * sectorsPerFat_;

    firstDataRegionSector_ =
        firstRootDirSector_ +
        bpb_.RootDirEntries
            /* in FAT32, the root directory is part of the data region */
            * sizeof(Structures::DirectoryEntry) / bpb_.BytesPerSector;

    sectorsInDataRegion_ = totalDevSectors_ - firstDataRegionSector_;
    totalDevClusters_    = sectorsInDataRegion_ / bpb_.SectorsPerCluster;

    bytesPerCluster_ = bpb_.SectorsPerCluster * bpb_.BytesPerSector;

    if (totalDevClusters_ < 4085)
        version_ = FileSystemVersion::Fat12;
    else if (totalDevClusters_ < 65525)
        version_ = FileSystemVersion::Fat16;
    else
        version_ = FileSystemVersion::Fat32;

    // copy FAT table to struct
    fat_.resize(sectorsInFatRegion_ * bpb_.BytesPerSector);

    fstream_.seekg(firstFatSector_ * bpb_.BytesPerSector);
    fstream_.read(reinterpret_cast<char *>(fat_.data()), fat_.size());

    endOfChainIndicator_ =
        ExtractCluster(1); // end of chain marker is stored in the
    // second entry of the FAT table

    if (version_ == FileSystemVersion::Fat32)
    {
        endOfChainIndicator_ &=
            0x0FFFFFFF; // some weird software fills the upper 4
        // bits, so we need to mask them out
    }
}

std::vector<Fatfs::FileInfo>
Fatfs::FileAllocationTable::Implementation::ReadDirectory(
    const std::string_view path)
{
    std::vector<FileInfo>                   dir{}; // user-readable directory
    std::vector<Structures::DirectoryEntry> rawDir =
        ReadRawDirectory(path); // "raw" directory (as it is on disk)

    // convert to user-readable directory
    std::transform(
        rawDir.begin(),
        rawDir.end(),
        std::back_inserter(dir),
        [&](const Structures::DirectoryEntry &x)
        {
            // convert name and extension to "normal" format
            const std::string filename =
                // 8 characters for name
                std::string(reinterpret_cast<const char *>(x.Name),
                            std::size(x.Name)) +
                // 3 characters for extension
                // 3 characters for extension
                std::string(reinterpret_cast<const char *>(x.Extension),
                            std::size(x.Extension));

            auto toTimeT = std::chrono::system_clock::to_time_t;

            FileInfo fi{};
            fi.Name = Helpers::Path::ConvertFatPathToLongPath(filename);

            fi.CreationTimestamp = toTimeT(
                Helpers::Time::ConvertFatTimeToUnixTime(x.CreationTime,
                                                        x.CreationDate));
            fi.LastModificationTimestamp =
                toTimeT(Helpers::Time::ConvertFatTimeToUnixTime(
                    x.LastModificationTime,
                    x.LastModificationDate));

            fi.LastAccessDate = toTimeT(
                Helpers::Time::ConvertFatTimeToUnixTime({}, x.LastAccessDate));

            fi.Size = x.FileSize;

            fi.IsDirectory =
                IsBitSet(x.Attributes, Structures::RawAttributes::Directory);

            return fi;
        });

    return dir;
}

std::vector<std::byte> Fatfs::FileAllocationTable::Implementation::ReadFile(
    const std::string_view path)
{
    return ReadFile(path, false);
}

std::vector<std::byte> Fatfs::FileAllocationTable::Implementation::ReadFile(
    const std::string_view path,
    const bool             isDirectory)
{
    std::string newPath = Utilities::String::TrimString(path);
    if (newPath.empty())
        throw Errors::InvalidPathError{"path is empty"};

    const std::vector<std::string> pathComponents =
        Helpers::Path::SplitLongPathToFatComponents(newPath);

    std::vector<std::byte>                  contents{};
    std::vector<Structures::DirectoryEntry> parent =
        ReadRawDirectory("\\"); // read root directory

    for (std::size_t i = 0; i < pathComponents.size(); ++i)
    {
        const auto &component = pathComponents[i];

        // find directory entry
        auto entry = std::find_if(
            parent.begin(),
            parent.end(),
            [&](const Structures::DirectoryEntry &dirEntry)
            {
                const bool hasSameName = // compare first 8 characters
                    std::memcmp(component.data(), dirEntry.Name, 8) == 0;
                const bool hasSameExtension = // compare last 3 characters
                    std::memcmp(component.data() + 8, dirEntry.Extension, 3) ==
                    0;

                bool condition = hasSameName && hasSameExtension;

                // if there are more path components, then this one must be a
                // directory otherwise, if is_directory is true then this one
                // must be a directory, else a file
                if (i < pathComponents.size() - 1 || isDirectory)
                    condition = condition &&
                                IsBitSet(dirEntry.Attributes,
                                         Structures::RawAttributes::Directory);

                return condition;
            });

        if (entry == parent.end())
        {
            if (i < pathComponents.size() - 1 || isDirectory)
            {
                throw Errors::DirectoryNotFoundError{
                    "directory '" +
                    Helpers::Path::ConvertFatPathToLongPath(component) +
                    "' not found"};
            }
            throw Errors::FileNotFoundError{
                "file '" + Helpers::Path::ConvertFatPathToLongPath(component) +
                "' not found"};
        }

        // if entry is file and there are more path components, then
        // throw exception
        if (i < pathComponents.size() - 1 &&
            !IsBitSet(entry->Attributes, Structures::RawAttributes::Directory))
        {
            throw Errors::InvalidFileOperationError{
                "file '" + Helpers::Path::ConvertFatPathToLongPath(component) +
                "' is not a directory, trying to browse contents of it"};
        }

        std::size_t cluster = entry->FirstClusterLow | entry->FirstClusterHigh
                                                           << 16;
        std::size_t newSize = 0;

        do
        {
            // seek to position of cluster in disk
            fstream_.seekg(ConvertClusterToSector(cluster) *
                           bpb_.BytesPerSector);

            newSize += bytesPerCluster_;
            contents.resize(newSize);

            // read cluster
            fstream_.read(reinterpret_cast<char *>(contents.data() + newSize -
                                                   bytesPerCluster_),
                          bytesPerCluster_);

            cluster = ExtractCluster(cluster);
        } while (!IsEndOfClusterChain(cluster));

        // if there are more path components, then set parent to contents
        if (i != pathComponents.size() - 1)
        {
            const std::byte *ptr = contents.data();

            const std::size_t len =
                contents.size() / sizeof(Structures::DirectoryEntry);
            const auto ptrAddr = reinterpret_cast<std::uintptr_t>(ptr);
            const auto arr =
                reinterpret_cast<const Structures::DirectoryEntry *>(ptrAddr);

            parent = std::vector(arr, arr + len);

            // get iterator to first null entry
            const auto kIterator =
                std::find_if(parent.begin(),
                             parent.end(),
                             [](const Structures::DirectoryEntry &x)
                             {
                                 return x.Name[0] == '\0';
                             });

            // remove null entries
            parent.erase(kIterator, parent.end());
        }

        // resize contents to actual size IF it is a file AND ONLY IF it is a
        // file
        if (i >= pathComponents.size() - 1 && !isDirectory)
            contents = {contents.data(), contents.data() + entry->FileSize};
    }

    return contents;
}

void Fatfs::FileAllocationTable::Implementation::CreateFile(
    std::string_view              path,
    const std::vector<std::byte> &data)
{
    std::size_t next =
        GetNextFreeCluster(); // save first cluster as we're going to create
                              // a new directory entry

    CreateDirectoryEntry(path, data, false);

    const std::size_t dataSizeInBytesRounded =
        RoundUp(data.size(), bytesPerCluster_);
    const std::size_t dataSizeInClusters =
        dataSizeInBytesRounded / bytesPerCluster_;

    std::vector<std::vector<std::byte>> clusterDivision{}; // data divided into
    // clusters
    clusterDivision.reserve(dataSizeInClusters);

    for (std::size_t i = 0; i < dataSizeInClusters; i++)
    {
        std::vector<std::byte> cluster{};
        cluster.reserve(bytesPerCluster_);

        for (std::size_t j = 0; j < bytesPerCluster_; j++)
        {
            // copy normally until we run out of data, then we pad with 0s
            if (i * bytesPerCluster_ + j < data.size())
                cluster.emplace_back(data[i * bytesPerCluster_ + j]);
            else
                cluster.emplace_back(std::byte{0});
        }

        clusterDivision.emplace_back(cluster);
    }

    // write FAT clusters
    std::vector<std::size_t> savedClusters{};
    savedClusters.reserve(dataSizeInClusters);

    // write clusters to buffer
    for (std::size_t i = 0; i < dataSizeInClusters; i++)
    {
        std::size_t current = next;

        // if we're at the last cluster, we need to set the end of chain marker
        next = i < dataSizeInClusters - 1 ? GetNextFreeCluster(current)
                                          : endOfChainIndicator_;

        SetCluster(current, next);
        savedClusters.emplace_back(current);
    }

    // write FAT
    for (int i = 0; i < bpb_.NumberOfFats; i++)
    {
        fstream_.seekp((firstFatSector_ + i * sectorsPerFat_) *
                       bpb_.BytesPerSector);
        fstream_.write(reinterpret_cast<const char *>(fat_.data()),
                       sectorsPerFat_ * bpb_.BytesPerSector);
    }

    // write data
    for (int i = 0; i < savedClusters.size(); i++)
    {
        fstream_.seekp(ConvertClusterToSector(savedClusters[i]) *
                       bpb_.BytesPerSector);
        fstream_.write(
            reinterpret_cast<const char *>(clusterDivision[i].data()),
            bytesPerCluster_);
    }
}

void Fatfs::FileAllocationTable::Implementation::CreateDirectory(
    std::string_view path)
{
    std::size_t cluster =
        GetNextFreeCluster(); // save first cluster as we're going to create
                              // a new directory entry

    CreateDirectoryEntry(path, {}, true);
    SetCluster(cluster, endOfChainIndicator_);

    // write FAT
    for (int i = 0; i < bpb_.NumberOfFats; i++)
    {
        fstream_.seekp((firstFatSector_ + i * sectorsPerFat_) *
                       bpb_.BytesPerSector);
        fstream_.write(reinterpret_cast<const char *>(fat_.data()),
                       sectorsPerFat_ * bpb_.BytesPerSector);
    }

    // create . and ..
    auto [time, date] = Helpers::Time::ConvertUnixTimeToFatTime(
        std::chrono::system_clock::now());

    const std::size_t entriesPerCluster =
        bytesPerCluster_ / sizeof(Structures::DirectoryEntry);

    std::vector<Structures::DirectoryEntry> entries(entriesPerCluster);
    auto                                    entry1 = entries.begin();

    entry1->Name[0] = '.';

    entry1->Attributes = Structures::RawAttributes::Directory;

    // set fields
    entry1->CreationDate         = date;
    entry1->CreationTime         = time;
    entry1->CreationTimeTenths   = 0;
    entry1->LastAccessDate       = date;
    entry1->FirstClusterHigh     = cluster >> 16; // high 16 bits
    entry1->LastModificationTime = time;
    entry1->LastModificationDate = date;
    entry1->FirstClusterLow      = cluster & 0xFFFF; // low 16 bits
    entry1->FileSize             = 0;

    // no difference between . and .. except for first cluster
    // get parent directory
    std::vector<std::string> pathComponents =
        Helpers::Path::SplitLongPathToFatComponents(path);
    std::ostringstream oss{};
    std::transform(pathComponents.begin(),
                   pathComponents.end() - 1,
                   std::ostream_iterator<std::string>(oss, "\\"),
                   Helpers::Path::ConvertFatPathToLongPath);

    const std::string                       parentDir = "\\" + oss.str();
    std::vector<Structures::DirectoryEntry> parent =
        ReadRawDirectory(parentDir);

    // root directory does not contain . and .. entries
    bool isParentRoot =
        std::strncmp(reinterpret_cast<const char *>(parent[0].Name),
                     ".          ",
                     11) != 0;

    auto entry2 = entries.begin() + 1;
    // copy entry1 to entry2
    std::memcpy(&*entry2, &*entry1, sizeof(Structures::DirectoryEntry));
    entry2->Name[1]          = '.';
    entry2->FirstClusterHigh = isParentRoot ? 0 : parent[0].FirstClusterHigh;
    entry2->FirstClusterLow  = isParentRoot ? 0 : parent[0].FirstClusterLow;

    // write directory
    fstream_.seekp(ConvertClusterToSector(cluster) * bpb_.BytesPerSector);
    fstream_.write(reinterpret_cast<const char *>(entries.data()),
                   bytesPerCluster_);
}

void Fatfs::FileAllocationTable::Implementation::DeleteEntry(
    std::string_view path) const
{
}

void Fatfs::FileAllocationTable::Implementation::EraseEntry(
    std::string_view path) const
{
}

Fatfs::FileSystemVersion
Fatfs::FileAllocationTable::Implementation::Version() const
{
    return version_;
}

void Fatfs::FileAllocationTable::Implementation::CreateDirectoryEntry(
    std::string_view              path,
    const std::vector<std::byte> &data, // only if file
    bool                          isDirectory)
{
    std::string newPath = Utilities::String::TrimString(path);
    if (newPath.empty())
        throw Errors::InvalidPathError{"path is empty"};

    bool exists = true;
    try
    {
        ReadFile(newPath, isDirectory);
    }
    catch (const Errors::FileNotFoundError &)
    {
        exists = false;
    }
    catch (const Errors::DirectoryNotFoundError &)
    {
        exists = false;
    }

    if (exists)
    {
        if (isDirectory)
            throw Errors::FileAlreadyExistsError{"directory " + newPath +
                                                 " already exists"};

        throw Errors::FileAlreadyExistsError{
            "file " + newPath +
            " already exists"}; // easy way out; we'll fix this later
    }

    // get parent directory
    std::vector<std::string> pathComponents =
        Helpers::Path::SplitLongPathToFatComponents(newPath);
    std::ostringstream oss{};
    // from first..(last - 1), convert each path to long path
    std::transform(pathComponents.begin(),
                   pathComponents.end() - 1,
                   std::ostream_iterator<std::string>(oss, "\\"),
                   Helpers::Path::ConvertFatPathToLongPath);

    std::vector<Structures::DirectoryEntry> parent =
        ReadRawDirectory("\\" + oss.str());

    const std::string &filename = pathComponents.back();

    // DirectoryEntry::name[0] == 0x20 is illegal
    if (filename[0] == ' ')
    {
        throw Errors::InvalidPathError{
            "first character of name shall not be 0x20 (or shall not start "
            "with a period)"};
    }

    std::size_t next = GetNextFreeCluster();

    auto [time, date] = Helpers::Time::ConvertUnixTimeToFatTime(
        std::chrono::system_clock::now());

    Structures::DirectoryEntry entry{};

    // copy filename
    for (int i = 0; i < 8; i++)
        entry.Name[i] = filename[i];

    // copy extension
    for (int i = 0; i < 3; i++)
        entry.Extension[i] = filename[i + 8];

    entry.Attributes = isDirectory ? Structures::RawAttributes::Directory
                                   : Structures::RawAttributes::Archive;

    // set fields
    entry.CreationDate         = date;
    entry.CreationTime         = time;
    entry.CreationTimeTenths   = 0;
    entry.LastAccessDate       = date;
    entry.FirstClusterHigh     = (next & 0xFFFF0000) >> 16; // high 16 bits
    entry.LastModificationTime = time;
    entry.LastModificationDate = date;
    entry.FirstClusterLow      = next & 0xFFFF; // low 16 bits
    entry.FileSize             = isDirectory ? 0 : data.size();

    const std::size_t oldDirSize = parent.size();

    parent.emplace_back(entry);

    const std::size_t newDirSize =
        parent.size(); // check if this one exceeds cluster boundary

    // get cluster of self
    std::size_t selfCluster =
        parent[0].Name[0] == '.'
            ? parent[0].FirstClusterHigh << 16 | parent[0].FirstClusterLow
            : 0; // if we're in the root directory, we don't have a cluster

    // check if exceeding the maximum number of entries in a directory IF
    // we're in the root directory and IF we're in a non-FAT32 volume
    if (selfCluster == 0 && version_ != FileSystemVersion::Fat32)
    {
        if (parent.size() > bpb_.RootDirEntries)
        {
            throw Errors::FileSystemError{"maximum number of entries in root "
                                          "directory exceeded"};
        }
    }

    auto p = selfCluster;

    // if root directory AND FAT32, get first cluster of root directory
    if (selfCluster == 0 && version_ == FileSystemVersion::Fat32)
        selfCluster = bpb_.Offset36.Fat32.FirstRootDirCluster;

    // get sector from cluster
    const std::size_t selfLba =
        p == 0 ? firstRootDirSector_ : ConvertClusterToSector(selfCluster);

    // only perform directory checks if either
    //   - we're not in the root directory
    //   - we're in the root directory, and we're in a FAT32 volume
    if (p >= 2 || (p == 0 && version_ == FileSystemVersion::Fat32))
    {
        std::vector<std::size_t> dirClusterChain =
            ExtractClusterChain(selfCluster);

        const std::size_t entriesPerCluster =
            bytesPerCluster_ / sizeof(Structures::DirectoryEntry);

        // if we're exceeding the cluster boundary, we need to allocate a new
        // cluster
        if (RoundUp(newDirSize, bytesPerCluster_) >
            RoundUp(oldDirSize, bytesPerCluster_))
        {
            std::size_t newCluster = GetNextFreeCluster(selfCluster);
            dirClusterChain.emplace_back(newCluster);
            SetCluster(selfCluster, newCluster);
            SetCluster(newCluster, endOfChainIndicator_);
        }

        const std::size_t newDirClusters =
            RoundUp(newDirSize, bytesPerCluster_) / bytesPerCluster_;

        // pad directory with 0s up until the next cluster boundary
        parent.resize(entriesPerCluster * newDirClusters);

        int i = 0;

        // write directory
        for (const auto &cluster : dirClusterChain)
        {
            fstream_.seekp(ConvertClusterToSector(cluster) *
                           bpb_.BytesPerSector);
            fstream_.write(reinterpret_cast<const char *>(
                               parent.data() + i * entriesPerCluster),
                           bytesPerCluster_);
            i++;
        }
    }
    else // we're in the root directory AND we're in a non-FAT32 volume
    // AND (thankfully) we're not exceeding the maximum number of entries
    {
        parent.resize(bpb_.RootDirEntries);

        // write directory
        fstream_.seekp(selfLba * bpb_.BytesPerSector);
        fstream_.write(reinterpret_cast<const char *>(parent.data()),
                       parent.size() * sizeof(Structures::DirectoryEntry));
    }
}

std::size_t Fatfs::FileAllocationTable::Implementation::ExtractCluster(
    size_t clusterNumber) const
{
    std::size_t cluster = clusterNumber;
    auto        saFat   = reinterpret_cast<std::uintptr_t>(
        fat_.data()); // to avoid strict aliasing

    switch (version_)
    {
    case FileSystemVersion::Fat12:
    {
        const auto orig = cluster;

        cluster = cluster * 3 / 2;
        cluster = *reinterpret_cast<const std::uint16_t *>(
            saFat + cluster); // basically, get 2 bytes from location of FAT +
        // cluster (no pointer arithmetic)

        if (orig % 2 == 0)
            cluster &= 0xFFF;
        else
            cluster >>= 4;
    }
    break;
    case FileSystemVersion::Fat16:
        cluster = reinterpret_cast<const std::uint16_t *>(saFat)[cluster];
        break;
    case FileSystemVersion::Fat32:
        cluster = reinterpret_cast<const std::uint32_t *>(saFat)[cluster];
        break;
    }

    return cluster;
}

void Fatfs::FileAllocationTable::Implementation::SetCluster(
    size_t clusterNumber,
    size_t next)
{
    std::size_t cluster = clusterNumber;
    auto        saFat   = reinterpret_cast<std::uintptr_t>(fat_.data());

    switch (version_)
    {
    case FileSystemVersion::Fat12:
    {
        cluster               = cluster * 3 / 2;
        const auto clusterPtr = reinterpret_cast<uint16_t *>(saFat + cluster);

        *clusterPtr |= cluster % 2 == 0
                           ? next << 4      // if even, store in upper 12 bits
                           : next & 0x0FFF; // else, store in low 12 bits
    }
    break;
    case FileSystemVersion::Fat16:
        reinterpret_cast<uint16_t *>(saFat)[cluster] = next;
        break;
    case FileSystemVersion::Fat32:
        reinterpret_cast<uint32_t *>(saFat)[cluster] = next;
        break;
    }
}

std::vector<std::size_t>
Fatfs::FileAllocationTable::Implementation::ExtractClusterChain(
    size_t startCluster) const
{
    std::vector<std::size_t> chain;
    std::size_t              cluster = startCluster;

    // pretty self-explanatory
    do
    {
        chain.emplace_back(cluster);
        cluster = ExtractCluster(cluster);
    } while (!IsEndOfClusterChain(cluster));

    return chain;
}

std::size_t Fatfs::FileAllocationTable::Implementation::GetNextFreeCluster(
    size_t startCluster) const
{
    auto saFat = reinterpret_cast<std::uintptr_t>(fat_.data());

    switch (version_)
    {
    case FileSystemVersion::Fat12:
    {
        for (std::size_t i = startCluster + 1; i < fat_.size() * 2 / 3; i++)
        {
            const auto  orig    = i;
            std::size_t cluster = i * 3 / 2;
            cluster = *reinterpret_cast<const uint16_t *>(saFat + cluster);

            if (orig % 2 == 0)
                cluster &= 0xFFF;
            else
                cluster >>= 4;

            if (cluster == 0)
                return i;
        }
    }
    break;
    case FileSystemVersion::Fat16:
    {
        for (std::size_t i = startCluster + 1;
             i < fat_.size() / 2 /* sizeof(uint16_t) */;
             i++)
        {
            if (const std::uint16_t cluster =
                    reinterpret_cast<const uint16_t *>(saFat)[i];
                cluster == 0)
            {
                return i;
            }
        }
    }
    break;
    default /* FAT32 */:
    {
        for (std::size_t i = startCluster + 1;
             i < fat_.size() / 4 /* sizeof(uint32_t) */;
             i++)
        {
            if (const std::uint32_t cluster =
                    reinterpret_cast<const uint32_t *>(saFat)[i];
                cluster == 0)
            {
                return i;
            }
        }
    }
    break;
    }

    return 0;
}

std::vector<Fatfs::Structures::DirectoryEntry>
Fatfs::FileAllocationTable::Implementation::ReadRawDirectory(
    std::string_view path)
{
    std::vector<Structures::DirectoryEntry>
        rawDir{}; // "raw" directory (as it is on disk)

    // if root path is given then return root directory
    if (Utilities::String::TrimString(path) == "\\")
    {
        if (version_ != FileSystemVersion::Fat32)
        {
            // root directory in FAT12 and FAT16 has a fixed size and is located
            // at a fixed offset (directly after the FAT table)
            rawDir.resize(bpb_.RootDirEntries *
                          sizeof(Structures::DirectoryEntry));

            fstream_.seekg(firstRootDirSector_ * bpb_.BytesPerSector);
            fstream_.read(reinterpret_cast<char *>(rawDir.data()),
                          rawDir.size());
        }
        else
        {
            const std::size_t entriesPerCluster =
                bpb_.SectorsPerCluster * bpb_.BytesPerSector /
                sizeof(Structures::DirectoryEntry); // just for readability

            std::size_t cluster = bpb_.Offset36.Fat32.FirstRootDirCluster;
            std::size_t newSize = 0;

            do
            {
                // seek to position of cluster in disk
                fstream_.seekg(ConvertClusterToSector(cluster) *
                               bpb_.BytesPerSector);

                newSize += entriesPerCluster;
                rawDir.resize(newSize);

                // read cluster
                fstream_.read(reinterpret_cast<char *>(rawDir.data() + newSize -
                                                       entriesPerCluster),
                              sizeof(Structures::DirectoryEntry) *
                                  entriesPerCluster);

                // get next cluster index
                cluster = ExtractCluster(cluster);
            } while (cluster < 0x0FFFFFF0);
            // indices greater than 0x0FFFFFF0 are either damaged or indicates
            // the last cluster in the chain
        }
    }
    else
    {
        const std::vector<std::byte> contents = ReadFile(path, true);

        const std::byte  *ptr = contents.data(); // pointer to contents
        const std::size_t len =
            contents.size() /
            sizeof(Structures::DirectoryEntry); // amount of entries
        const auto arr = reinterpret_cast<const Structures::DirectoryEntry *>(
            ptr); // cast to
        // array of
        // DirectoryEntry

        rawDir.assign(arr, arr + len);
    }

    // get iterator to first null entry
    const auto it = std::find_if(rawDir.begin(),
                                 rawDir.end(),
                                 [](const Structures::DirectoryEntry &x)
                                 {
                                     return x.Name[0] == '\0';
                                 });

    // remove null entries
    rawDir.erase(it, rawDir.end());

    return rawDir;
}

std::size_t Fatfs::FileAllocationTable::Implementation::ConvertClusterToSector(
    size_t cluster) const
{
    return (cluster - 2) * bpb_.SectorsPerCluster + firstDataRegionSector_;
}

bool Fatfs::FileAllocationTable::Implementation::IsEndOfClusterChain(
    size_t cluster) const
{
    return (version_ == FileSystemVersion::Fat12 && cluster >= 0x0FF0 &&
            cluster <= 0x0FFF) ||
           (version_ == FileSystemVersion::Fat16 && cluster >= 0xFFF0 &&
            cluster <= 0xFFFF) ||
           (version_ == FileSystemVersion::Fat32 && cluster >= 0x0FFFFFF0 &&
            cluster <= 0x0FFFFFFF);
}
