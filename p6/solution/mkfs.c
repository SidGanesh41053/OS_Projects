/*
Part 1 Thought Process:
1. Parse Input Arguments
    Read all input args to determine
        - RAID mode
        - Disk image files
        - Number of inodes
        - Number of blocks
    Validate inputs
    - ensure that the RAID mode is specified
    - Ensure at least 2 disk image files are provided
    - Ensure the disk image files are large enough to accomomodate metadata and requested blocks

2. Validate Disk Image Files
- Open the disk image files and check if their sizes are sufficient to hold the required filesystem structures
    - Superblock
    - Inode bitmap
    - Data block bitmap
    - Inodes (each inode takes 512 bytes)
    - Data blocks (512 bytes each)
- If insufficient space, exit program with return code -1

3. Write the Superblock
- The first structure written to the disk. Contains metadata about the filesystem, such as:
    - Number of inodes
    - Number of data blocks
    - RAID mode
    - Info about the order of disks (for raid 0)
- Also need to modify struct wfs_sb provided in wfs.h to include fields for:
    - RAID mode
    - Disk order (if applicable for RAID 0), by time

4. Write the bitmaps
    1. Inode Bitmap: each bit represents whether an inode is allocated (1) or free (0). Initially, all are free
    2. Data block Bitmap: each bit represents whether a data block is allocated or free. Initially, all are free
- The size of bitmaps depend on number of inodes and data blocks

5. Write the Root inode
- the root directory ("/") is always the FIRST inode:
- Allocate this inode and initialize it with:
    - st_mode => set the file mode to indicate it's a directory
    - st_size => set the size of the directory to 0
    - Direct pointers to NULL
    - Indirect pointer to NULL

6. Write the metadata to all disks
- If RAID 0: meta data (superblock, bitmaps, root inode) is mirrored across all disks
- If RAID 1/1v: metadata is also mirrored across all disks; don't need to handle verification logic for 1v, handled in part 2
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "wfs.h"

/**
 * Rounds up the given number of blocks to the nearest multiple of 32.
 * Ensures that the number of blocks is aligned to a
 * multiple of 32.
 */
int round_up_blocks(int num_blocks) {
    int remainder = num_blocks % 32;
    if (remainder != 0) {
        num_blocks += (32 - remainder);
    }
    return num_blocks;
}

int main(int argc, char *argv[]) {
    int raid_mode = -1;
    int num_inodes = -1;
    int num_blocks = -1;
    char *disk_files[MAX_DISKS] = {0};
    int num_disks = 0;

    // input parsing using getopt instead of fgets or traditional CL arg reading
    // https://www.geeksforgeeks.org/getopt-function-in-c-to-parse-command-line-arguments/
    int opt;
    while ((opt = getopt(argc, argv, "r:d:i:b:")) != -1) {
        switch (opt) {
        case 'r':
            if (strcmp(optarg, "0") == 0) raid_mode = 0;
                else if (strcmp(optarg, "1") == 0) raid_mode = 1;
                else if (strcmp(optarg, "1v") == 0) raid_mode = 2;
                else {
                    fprintf(stderr, "Invalid RAID mode.\n");
                    return 1;
                }
                break;

        case 'd':
            if (num_disks >= MAX_DISKS) {
                fprintf(stderr, "Too many disks specified.\n");
                return 1;
            }
            if (strlen(optarg) > MAX_NAME) {
                fprintf(stderr, "Disk filename exceeds maximum length.\n");
                return 1;
            }
            disk_files[num_disks++] = optarg;
            break;

        case 'i':
            num_inodes = atoi(optarg);
            if (num_inodes <= 0) {
                fprintf(stderr, "Invalid number of inodes.\n");
                return 1;
            }
            break;

        case 'b':
            num_blocks = atoi(optarg);
            if (num_blocks <= 0) {
                fprintf(stderr, "Invalid number of data blocks.\n");
                return 1;
            }
            break;

        default:
            fprintf(stderr, "Usage: %s -r [0|1|1v] -d <disk> ... -i <num_inodes> -b <num_blocks>\n", argv[0]);
            return 1;
        }
    }

    if (raid_mode == -1 || num_disks == 0 || num_inodes == -1 || num_blocks == -1) {
        fprintf(stderr, "Missing required arguments.\n");
        return 1;
    }

    if ((raid_mode == 1 || raid_mode == 2) && num_disks < 2) {
        fprintf(stderr, "Not enough disks for RAID mode.\n");
        return 1;
    }

    // init each disk, move loop to outside for simplicity
    for (int i = 0; i < num_disks; i++) {
        // aligning inodes and data blocks with block align
        num_inodes = round_up_blocks(num_inodes);
        num_blocks = round_up_blocks(num_blocks);

        off_t i_bitmap_ptr = sizeof(struct wfs_sb);
        off_t d_bitmap_ptr = i_bitmap_ptr + (num_inodes / 8);
        off_t i_blocks_ptr = ((d_bitmap_ptr + (num_blocks / 8) + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
        off_t d_blocks_ptr = i_blocks_ptr + (num_inodes * BLOCK_SIZE);
        size_t fs_size = (num_inodes * BLOCK_SIZE) + (num_blocks * BLOCK_SIZE) + i_blocks_ptr;
 
        int fd = open(disk_files[i], O_RDWR);
        if (fd == -1) {
            perror("open");
            return -1;
        }

        struct stat st;
        if (fstat(fd, &st) == -1) {
            perror("fstat");
            return -1;
        }

        if (st.st_size < fs_size) {
            // fprintf(stderr, "Disk image %s is too small.\n", disk_files[i]);
            close(fd);
            return -1;
        }

        struct wfs_sb superblock = {
            .num_inodes = num_inodes,
            .num_data_blocks = num_blocks,
            .i_bitmap_ptr = i_bitmap_ptr,
            .d_bitmap_ptr = d_bitmap_ptr,
            .i_blocks_ptr = i_blocks_ptr,
            .d_blocks_ptr = d_blocks_ptr,
            .tim = (int)time(NULL),
            .raid_mode = raid_mode,
            .disk_id = i,
        };

        // write sb to disk
        if (write(fd, &superblock, sizeof(struct wfs_sb)) == -1) {
            perror("write");
            close(fd);
            return -1;
        }

        // define root inode
        struct wfs_inode root_inode = {
            .mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR,
            .uid = getuid(),
            .gid = getgid(),
            .size = 0,
            .nlinks = 1,
            .atim = time(NULL),
            .mtim = time(NULL),
            .ctim = time(NULL)
        };

        // init inode bitmap
        if (lseek(fd, superblock.i_bitmap_ptr, SEEK_SET) == (off_t)-1) {
            perror("lseek");
            close(fd);
            return -1;
        }
        if (write(fd, &(uint32_t){1 << 0}, sizeof(uint32_t)) == -1) {
            perror("write");
            close(fd);
            return -1;
        }

        // write root node
        if (lseek(fd, superblock.i_blocks_ptr, SEEK_SET) == (off_t)-1) {
            perror("lseek");
            close(fd);
            return -1;
        }
        if (write(fd, &root_inode, sizeof(struct wfs_inode)) < 0) {
            perror("write");
            close(fd);
            return -1;
        }

        close(fd);
    }

    printf("Success\n");
    return 0;
}