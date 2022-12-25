# fatfs

A program to manage FAT volumes (FAT12, FAT16, FAT32.)

Originally written to fulfill a proof-of-concept for a research project.

## Usage

```
fatfs <volume> <command> <args...>
```

### Commands

#### `read`

Reads a file from the volume and prints it to stdout.

```
fatfs <volume> read <file>
```

#### `create`

Creates a file or directory

```
fatfs <volume> create [-d <directory>|<file> <data>]
```

#### `view`

Prints the contents of a directory to stdout.

```
fatfs <volume> view <directory>
```