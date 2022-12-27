
#ifndef FATFS_STRUCTURES_HPP
#define FATFS_STRUCTURES_HPP

#include <cstdint>

namespace Fatfs
{
namespace Structures
{

namespace RawAttributes
{
enum // scoped enum
{
    ReadOnly   = 0x01,
    Hidden     = 0x02,
    SystemFile = 0x04,
    VolumeId   = 0x08,
    Directory  = 0x10,
    Archive    = 0x20,
    LongName   = ReadOnly | Hidden | SystemFile | VolumeId
};
} // namespace RawAttributes

#pragma pack(push, 1)
struct DateFormat
{
    std::uint8_t Day   : 5;
    std::uint8_t Month : 4;
    std::uint8_t Year  : 7; // since 1980
};

struct TimeFormat
{
    std::uint8_t Second : 5;
    std::uint8_t Minute : 6;
    std::uint8_t Hour   : 5;
};

struct BiosParameterBlock
{
    std::uint8_t  Jmp[3];
    std::uint8_t  OemName[8];
    std::uint16_t BytesPerSector;
    std::uint8_t  SectorsPerCluster;
    std::uint16_t ReservedSectors;
    std::uint8_t  NumberOfFats;
    std::uint16_t RootDirEntries;
    std::uint16_t TotalSectors;
    std::uint8_t  MediaDescriptor;
    std::uint16_t SectorsPerFat;
    std::uint16_t SectorsPerTrack;
    std::uint16_t NumberOfHeads;
    std::uint32_t NumberOfHiddenSectors;
    std::uint32_t TotalSectorsLarge;

    union
    {
        struct
        {
            std::uint8_t  DriveNumber;
            std::uint8_t  Reserved;
            std::uint8_t  BootSignature;
            std::uint32_t VolumeId;
            std::uint8_t  VolumeLabel[11];
            std::uint8_t  FileSystemType[8];
        } Fat12Or16;

        struct
        {
            std::uint32_t SectorsPerFat;
            std::uint16_t ExtendedFlags;
            std::uint16_t FileSystemVersion;
            std::uint32_t FirstRootDirCluster;
            std::uint16_t FsInfo;
            std::uint16_t FirstBackupBootSector;
            std::uint8_t  Zero[12];
            std::uint8_t  DriveNumber;
            std::uint8_t  Reserved;
            std::uint8_t  BootSignature;
            std::uint32_t VolumeId;
            std::uint8_t  VolumeLabel[11];
            std::uint8_t  FileSystemType[8];
        } Fat32;
    } Offset36;
};

struct DirectoryEntry
{
    std::uint8_t  Name[8];
    std::uint8_t  Extension[3];
    std::uint8_t  Attributes;
    std::uint8_t  Reserved;
    std::uint8_t  CreationTimeTenths;
    TimeFormat    CreationTime;
    DateFormat    CreationDate;
    DateFormat    LastAccessDate;
    std::uint16_t FirstClusterHigh;
    TimeFormat    LastModificationTime;
    DateFormat    LastModificationDate;
    std::uint16_t FirstClusterLow;
    std::uint32_t FileSize;
};

#pragma pack(pop)

} // namespace Structures
} // namespace Fatfs

#endif // FATFS_STRUCTURES_HPP
