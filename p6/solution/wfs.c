#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>
#include <inttypes.h>
#include "wfs.h"
#include <time.h>
#include <assert.h>

void *disk_img[MAX_DISKS];
int num_disks = 0;
int raid_mode;
int disk_order[MAX_DISKS];

/* =========================== HELPER FUNCTIONS =========================== */
ssize_t allocate_block(uint32_t *bitmap, size_t length) {
    size_t total_bits = length * 32;
    for (size_t bit_num = 0; bit_num < total_bits; bit_num++) {
        size_t array_index = bit_num / 32;
        size_t bit_offset = bit_num % 32;

        uint32_t mask = (uint32_t)1 << bit_offset;
        if ((bitmap[array_index] & mask) == 0) {
            bitmap[array_index] |= mask;
            return (ssize_t)bit_num;
        }
    }
    return -1;
}

off_t allocate_data_block(int disk_id) {
    struct wfs_sb *superb = (struct wfs_sb *)disk_img[disk_id];
    uint32_t *data_bmp = (uint32_t *)((char *)disk_img[disk_id] + superb->d_bitmap_ptr);

    ssize_t block_idx = allocate_block(data_bmp, superb->num_data_blocks / 32);
    if (block_idx < 0) {
        fprintf(stderr, "[ERROR] allocate_data_block: Could not allocate data block on disk %d.\n", disk_id);
        return 0;
    }
    off_t chosen_offset = superb->d_blocks_ptr + (BLOCK_SIZE * block_idx);
    return chosen_offset;
}

struct wfs_inode *allocate_inode(int disk_id) {
    struct wfs_sb *superb = (struct wfs_sb *)disk_img[disk_id];
    uint32_t *inode_bmp = (uint32_t *)((char *)disk_img[disk_id] + superb->i_bitmap_ptr);

    ssize_t num_block = allocate_block(inode_bmp, superb->num_inodes / 32);
    if (num_block < 0) {
        fprintf(stderr, "Failed to allocate an inode on disk %d.\n", disk_id);
        return NULL;
    }

    struct wfs_inode *new_inode = (struct wfs_inode *)((char *)disk_img[disk_id] + superb->i_blocks_ptr + BLOCK_SIZE * num_block);
    new_inode->num = num_block;
    return new_inode;
}

void free_block(off_t block_offset, int disk_id) {
    struct wfs_sb *superb = (struct wfs_sb *)disk_img[disk_id];

    void *target_block = (char *)disk_img[disk_id] + block_offset;
    memset(target_block, 0, BLOCK_SIZE);

    uint32_t blk_pos = (uint32_t)((block_offset - superb->d_blocks_ptr) / BLOCK_SIZE);
    uint32_t *data_bmp = (uint32_t *)((char *)disk_img[disk_id] + superb->d_bitmap_ptr);

    data_bmp[blk_pos / 32] &= ~(1U << (blk_pos % 32));
}

void free_inode(struct wfs_inode *inode, int disk_id) {
    struct wfs_sb *superb = (struct wfs_sb *)disk_img[disk_id];
    memset((char *)inode, 0, sizeof(struct wfs_inode));
    uint32_t inode_pos = ((char *)inode - ((char *)disk_img[disk_id] + superb->i_blocks_ptr)) / BLOCK_SIZE;
    if (inode_pos >= superb->num_inodes) {
        fprintf(stderr, "[ERROR] free_inode: Inode pos %u out of range on disk %d\n", inode_pos, disk_id);
        return;
    }
    uint32_t *inode_bmp = (uint32_t *)((char *)disk_img[disk_id] + superb->i_bitmap_ptr);
    inode_bmp[inode_pos / 32] &= ~(1U << (inode_pos % 32));
    fprintf(stderr, "[DEBUG] free_inode: Freed inode %u on disk %d\n", inode_pos, disk_id);
}

struct wfs_inode *get_inode_by_number(int inode_num, int disk_id) {
    struct wfs_sb *superb = (struct wfs_sb *)((char *)disk_img[disk_id]);
    uint32_t *inode_bmp = (uint32_t *)((char *)disk_img[disk_id] + superb->i_bitmap_ptr);

    int word_idx = inode_num / 32;
    int bit_pos = inode_num % 32;

    if (inode_bmp[word_idx] & (1U << bit_pos)) {
        return (struct wfs_inode *)((char *)disk_img[disk_id] + superb->i_blocks_ptr + inode_num * BLOCK_SIZE);
    }
    return NULL;
}

char *calculate_block_offset(struct wfs_inode *node, off_t off, int should_alloc, int disk_id) {
    int blk_num = off / BLOCK_SIZE;
    off_t *blk_array;

    if (blk_num > D_BLOCK + (BLOCK_SIZE / sizeof(off_t))) {
        fprintf(stderr, "Error: Block number %d exceeds supported range.\n", blk_num);
        return NULL;
    }

    if (raid_mode == 0) {
        int target_disk = blk_num % num_disks;

        if (blk_num > D_BLOCK) {
            blk_num -= IND_BLOCK;
            if (node->blocks[IND_BLOCK] == 0) {
                for (int i = 0; i < num_disks; i++) {
                    struct wfs_inode *temp_node = get_inode_by_number(node->num, i);
                    temp_node->blocks[IND_BLOCK] = allocate_data_block(i);
                }
            }
            blk_array = (off_t *)((char *)disk_img[disk_id] + node->blocks[IND_BLOCK]);
        } else {
            blk_array = node->blocks;
        }

        if (should_alloc && blk_array[blk_num] == 0) {
            off_t new_block_offset = allocate_data_block(target_disk);
            for (int i = 0; i < num_disks; i++) {
                struct wfs_inode *temp_node = get_inode_by_number(node->num, i);
                if (blk_array != node->blocks) {
                    off_t *indirect = (off_t *)((char *)disk_img[i] + temp_node->blocks[IND_BLOCK]);
                    indirect[blk_num] = new_block_offset;
                } else {
                    temp_node->blocks[blk_num] = new_block_offset;
                }
            }
        }

        return (char *)disk_img[target_disk] + blk_array[blk_num] + (off % BLOCK_SIZE);

    } else {
        // RAID 1 or RAID 1v
        if (blk_num > D_BLOCK) {
            blk_num -= IND_BLOCK;
            if (node->blocks[IND_BLOCK] == 0) {
                node->blocks[IND_BLOCK] = allocate_data_block(disk_id);
            }
            blk_array = (off_t *)((char *)disk_img[disk_id] + node->blocks[IND_BLOCK]);
        } else {
            blk_array = node->blocks;
        }

        if (should_alloc && blk_array[blk_num] == 0) {
            blk_array[blk_num] = allocate_data_block(disk_id);
        }

        if (blk_array[blk_num] == 0) {
            fprintf(stderr, "Error: Block allocation failed!\n");
            return NULL;
        }

        return (char *)disk_img[disk_id] + blk_array[blk_num] + (off % BLOCK_SIZE);
    }
}

int remove_dentry(struct wfs_inode *node, int inode_num, int disk_id) {
    size_t directory_size = node->size;
    struct wfs_dentry *dir_entries;

    for (off_t current_off = 0; current_off < (off_t)directory_size; current_off += sizeof(struct wfs_dentry)) {
        dir_entries = (struct wfs_dentry *)calculate_block_offset(node, current_off, 0, disk_id);

        if (dir_entries->num == inode_num) {
            dir_entries->num = 0;
            return 0;
        }
    }
    return -1;
}

int add_dentry(struct wfs_inode *parent_node, int inode_num, char *fname, int disk_id) {
    off_t scan_offset = 2 * sizeof(struct wfs_dentry);

    while (scan_offset < parent_node->size) {
        char *blk_ptr = calculate_block_offset(parent_node, scan_offset, 0, disk_id);
        if (blk_ptr == NULL) {
            fprintf(stderr, "[ERROR] add_dentry: Failed at offset %ld\n", scan_offset);
            return -EIO;
        }

        struct wfs_dentry *dir_entries = (struct wfs_dentry *)blk_ptr;
        if (dir_entries->num == 0) {
            dir_entries->num = inode_num;
            strncpy(dir_entries->name, fname, MAX_NAME);
            dir_entries->name[MAX_NAME - 1] = '\0';

            if (raid_mode == 0) {
                for (int i = 0; i < num_disks; i++) {
                    struct wfs_inode *temp_node = get_inode_by_number(parent_node->num, i);
                    if (temp_node) {
                        temp_node->nlinks++;
                    }
                }
            } else {
                parent_node->nlinks++;
            }
            return 0;
        }
        scan_offset += sizeof(struct wfs_dentry);
    }

    struct wfs_dentry *new_entries = (struct wfs_dentry *)calculate_block_offset(parent_node, parent_node->size, 1, disk_id);
    if (!new_entries) {
        fprintf(stderr, "[ERROR] add_dentry: Failed to allocate new directory block\n");
        return -ENOSPC;
    }
    new_entries->num = inode_num;
    strncpy(new_entries->name, fname, MAX_NAME);
    new_entries->name[MAX_NAME - 1] = '\0';

    if (raid_mode == 0) {
        for (int i = 0; i < num_disks; i++) {
            struct wfs_inode *temp_node = get_inode_by_number(parent_node->num, i);
            if (temp_node) {
                temp_node->nlinks++;
                temp_node->size += BLOCK_SIZE;
            }
        }
    } else {
        parent_node->nlinks++;
        parent_node->size += BLOCK_SIZE;
    }

    return 0;
}

void initialize_inode(struct wfs_inode *inode, mode_t mode) {
    inode->mode = mode;
    inode->uid = getuid();
    inode->gid = getgid();
    inode->size = 0;
    inode->nlinks = 1;
    inode->atim = (int)time(NULL);
    inode->mtim = (int)time(NULL);
    inode->ctim = (int)time(NULL);
}

int lookup_inode(struct wfs_inode *current_node, char *rel_path, struct wfs_inode **result_node, int disk_id) {
    if (!strcmp(rel_path, "")) {
        *result_node = current_node;
        return 0;
    }

    char *component = rel_path;
    while (*rel_path != '/' && *rel_path != '\0') {
        rel_path++;
    }
    if (*rel_path != '\0') {
        *rel_path++ = '\0';
    }

    size_t dir_bytes = current_node->size;
    struct wfs_dentry *entry_list;
    int found_inum = -1;

    for (off_t scan_off = 0; scan_off < (off_t)dir_bytes; scan_off += sizeof(struct wfs_dentry)) {
        entry_list = (struct wfs_dentry *)calculate_block_offset(current_node, scan_off, 0, disk_id);

        if (entry_list->num != 0 && !strcmp(entry_list->name, component)) {
            found_inum = entry_list->num;
            break;
        }
    }

    if (found_inum < 0) {
        return -1;
    }

    struct wfs_inode *next_node = get_inode_by_number(found_inum, disk_id);
    return lookup_inode(next_node, rel_path, result_node, disk_id);
}

int lookup_inode_by_path(char *abs_path, struct wfs_inode **out_node, int disk_id) {
    // start from root inode, skip the leading '/'
    struct wfs_inode *root_node = get_inode_by_number(0, disk_id);
    return lookup_inode(root_node, abs_path + 1, out_node, disk_id);
}

/* ================================ GETATTR =============================== */
int wfs_getattr(const char *path, struct stat *stbuf) {
    printf("[DEBUG] enter wfs_getattr\n");

    struct wfs_inode *inode;
    char *path_copy = strdup(path);
    if (lookup_inode_by_path(path_copy, &inode, 0) < 0) {
        free(path_copy);
        return -ENOENT;
    }

    stbuf->st_uid = inode->uid;
    stbuf->st_gid = inode->gid;
    stbuf->st_atime = inode->atim;
    stbuf->st_mtime = inode->mtim;
    stbuf->st_ctime = inode->ctim;
    stbuf->st_size = inode->size;
    stbuf->st_nlink = inode->nlinks;
    stbuf->st_mode = inode->mode;
    stbuf->st_blocks = (stbuf->st_size + 511) / 512;

    printf("[DEBUG] wfs_getattr: inode %d, st_mode = %o, st_blocks = %ld\n", inode->num, stbuf->st_mode, stbuf->st_blocks);
    printf("[DEBUG] wfs_getattr: Returning success, inode %d\n", inode->num);

    free(path_copy);
    return 0;
}

/* ================================ MKNOD ================================= */
int wfs_mknod(const char *file_path, mode_t permissions, dev_t device) {
    if (raid_mode == 0) {
        printf("[LOG] creating file entry in RAID 0 mode\n");

        struct wfs_inode *parent_node = NULL;
        char *dir_path = strdup(file_path);
        char *entry_name = strdup(file_path);

        if (lookup_inode_by_path(dirname(dir_path), &parent_node, 0) < 0) {
            fprintf(stderr, "[ERROR] Parent inode for %s not found\n", dirname(dir_path));
            free(dir_path);
            free(entry_name);
            return -ENOENT;
        }

        struct wfs_inode *new_node = NULL;
        for (int disk_index = 0; disk_index < num_disks; disk_index++) {
            new_node = allocate_inode(disk_index);
            if (!new_node) {
                fprintf(stderr, "[ERROR] Insufficient space to allocate inode on disk %d\n", disk_index);
                free(dir_path);
                free(entry_name);
                return -ENOSPC;
            }
            initialize_inode(new_node, S_IFREG | permissions);
        }

        if (add_dentry(parent_node, new_node->num, basename(entry_name), 0) < 0) {
            fprintf(stderr, "[ERROR] Failed to add directory entry for %s\n", basename(entry_name));
            free(dir_path);
            free(entry_name);
            return -EEXIST;
        }

        free(dir_path);
        free(entry_name);
        return 0;
    }

    for (int disk_index = 0; disk_index < num_disks; disk_index++) {
        printf("[LOG] create_file_entry in RAID 1 mode\n");

        struct wfs_inode *parent_node = NULL;
        char *dir_path = strdup(file_path);
        char *entry_name = strdup(file_path);

        if (lookup_inode_by_path(dirname(dir_path), &parent_node, disk_index) < 0) {
            fprintf(stderr, "[ERROR] Parent inode for %s not found\n", dirname(dir_path));
            free(dir_path);
            free(entry_name);
            return -ENOENT;
        }

        struct wfs_inode *new_node = allocate_inode(disk_index);
        if (!new_node) {
            fprintf(stderr, "[ERROR] Insufficient space to allocate inode on disk %d\n", disk_index);
            free(dir_path);
            free(entry_name);
            return -ENOSPC;
        }
        initialize_inode(new_node, S_IFREG | permissions);

        if (add_dentry(parent_node, new_node->num, basename(entry_name), disk_index) < 0) {
            fprintf(stderr, "[ERROR] Failed to add directory entry for %s\n", basename(entry_name));
            free(dir_path);
            free(entry_name);
            return -EEXIST;
        }

        free(dir_path);
        free(entry_name);
    }
    return 0;
}

/* ================================ MKDIR =============================== */
int wfs_mkdir(const char *path, mode_t mode) {
    printf("[DEBUG] enter wfs_mkdir\n");

    int ret;
    for (int i = 0; i < (raid_mode == 0 ? 1 : num_disks); i++) {
        struct wfs_inode *parent_inode = NULL;

        char *base_directory_path = strdup(path);
        char *directory_name = strdup(path);

        if (lookup_inode_by_path(dirname(base_directory_path), &parent_inode, i) < 0) {
            fprintf(stderr, "[ERROR]: Cannot find parent inode!\n");
            free(base_directory_path);
            free(directory_name);
            ret = -ENOENT;
        }

        struct wfs_inode *inode = NULL;
        if (raid_mode == 1 || raid_mode == 2) {
            inode = allocate_inode(i);
            if (inode == NULL) {
                free(base_directory_path);
                free(directory_name);
                ret = -ENOSPC;

                fprintf(stderr, "[ERROR]: Failed to allocate inode!\n");
            }
            initialize_inode(inode, S_IFDIR | mode);
        } else {
            for (int i = 0; i < num_disks; i++) {
                inode = allocate_inode(i);
                if (inode == NULL) {
                    free(base_directory_path);
                    free(directory_name);
                    ret = -ENOSPC;
                    
                    fprintf(stderr, "[ERROR]: Failed to allocate inode!\n");
                }
                initialize_inode(inode, S_IFDIR | mode);
            }
        }

        if (add_dentry(parent_inode, inode->num, basename(directory_name), i) < 0) {
            fprintf(stderr, "Error: Cannot add directory entry for '%s' in parent inode.\n", basename(directory_name));
            free(base_directory_path);
            free(directory_name);
            ret = -ENOENT;
        }

        free(base_directory_path);
        free(directory_name);
        ret = 0;
    }

    return ret;
}

/* ======================= ADDITIONAL HELPER FUNCTIONS ======================= */
// Load an inode into the given struct from disk 0 (as reference)
int load_inode(int inum, struct wfs_inode *out) {
    // In RAID 1 and RAID 1v, inodes are identical across all disks
    // In RAID 0, each inode block is also mirrored (assuming meta is always mirrored)
    // We'll just read from disk 0
    struct wfs_sb *superb = (struct wfs_sb *)disk_img[0];
    struct wfs_inode *inode_ptr = (struct wfs_inode *)((char *)disk_img[0] + superb->i_blocks_ptr + inum * BLOCK_SIZE);
    memcpy(out, inode_ptr, sizeof(struct wfs_inode));
    return 0;
}

// Store inode updates to all disks
int store_inode(int inum, struct wfs_inode *in) {
    struct wfs_sb *superb = (struct wfs_sb *)disk_img[0];
    for (int d = 0; d < num_disks; d++) {
        memcpy((char *)disk_img[d] + superb->i_blocks_ptr + inum * BLOCK_SIZE, in, sizeof(struct wfs_inode));
    }
    return 0;
}

// Freed a given inode number from all disks
void free_inode_all_disks(int inum) {
    fprintf(stderr, "[DEBUG] free_inode_all_disks: Freeing inode %d on all disks\n", inum);
    for (int d = 0; d < num_disks; d++) {
        struct wfs_inode *w = get_inode_by_number(inum, d);
        if (w) {
            fprintf(stderr, "[DEBUG] free_inode_all_disks: Freeing inode %d on disk %d\n", inum, d);
            free_inode(w, d);
        } else {
            fprintf(stderr, "[ERROR] free_inode_all_disks: Failed to retrieve inode %d on disk %d\n", inum, d);
        }
    }
}

// Free a data block from all disks in RAID 1/1v, or appropriate disk in RAID 0
void free_data_block_all_disks(off_t blk) {
    // In RAID 1/1v, free from all disks
    // In RAID 0, we must figure out which disk to free from. But since the code doesn't handle striped logic in detail, we'll assume all mirrored.
    // The original code for RAID 0 might differ, but let's assume we free from all disks for simplicity.
    fprintf(stderr, "[DEBUG] free_data_block_all_disks: Freeing block at offset %ld on all disks\n", blk);
    for (int d = 0; d < num_disks; d++) {
        free_block(blk, d);
    }
}

// Find dentry by name in a directory inode
int find_dentry(struct wfs_inode *dir_inode, const char *name, struct wfs_dentry *dentry) {
    size_t sz = dir_inode->size;
    int disk = 0; // Meta operations on disk 0

    for (off_t off = 0; off < sz; off += sizeof(struct wfs_dentry)) {
        char *block_ptr = calculate_block_offset(dir_inode, off, 0, disk);
        if (block_ptr == NULL) {
            fprintf(stderr, "[ERROR] find_dentry: calculate_block_offset returned NULL for offset %ld\n", off);
            return -EIO;
        }

        struct wfs_dentry *entries = (struct wfs_dentry *)block_ptr;

        if (entries->num != 0 && strcmp(entries->name, name) == 0) {
            if (dentry) *dentry = *entries;
            return 0;
        }
    }

    return -ENOENT;
}

// Remove a dentry by name
int remove_dentry_by_name(struct wfs_inode *dir_inode, const char *name) {
    size_t sz = dir_inode->size;
    int disk = 0;
    fprintf(stderr, "[DEBUG] remove_dentry_by_name: Looking for '%s' in inode %d\n", name, dir_inode->num);

    for (off_t off = 0; off < sz; off += sizeof(struct wfs_dentry)) {
        char *block_ptr = calculate_block_offset(dir_inode, off, 0, disk);
        if (block_ptr == NULL) {
            fprintf(stderr, "[ERROR] remove_dentry_by_name: calculate_block_offset returned NULL for offset %ld\n", off);
            return -EIO;
        }

        struct wfs_dentry *entries = (struct wfs_dentry *)block_ptr;
        fprintf(stderr, "[DEBUG] remove_dentry_by_name: Checking dentry at offset %ld: num=%d, name='%s'\n", off, entries->num, entries->name);

        if (entries->num != 0 && strcmp(entries->name, name) == 0) {
            memset(entries, 0, sizeof(struct wfs_dentry));
            fprintf(stderr, "[DEBUG] remove_dentry_by_name: Successfully removed dentry '%s'\n", name);
            return 0;
        }
    }

    fprintf(stderr, "[ERROR] remove_dentry_by_name: Dentry '%s' not found\n", name);
    return -ENOENT;
}

// Free indirect blocks if any
int free_indirect_blocks(struct wfs_inode *inode) {
    if (inode->blocks[IND_BLOCK] == 0) {
        return 0;
    }

    // Indirect block: contains off_t pointers
    off_t ind_blk_off = inode->blocks[IND_BLOCK];
    // We assume one indirect block with multiple entries
    off_t *ind_arr = (off_t *)((char *)disk_img[0] + ind_blk_off);

    for (int i = 0; i < (BLOCK_SIZE / sizeof(off_t)); i++) {
        if (ind_arr[i] != 0) {
            free_data_block_all_disks(ind_arr[i]);
        }
    }
    // Free the indirect block itself
    free_data_block_all_disks(inode->blocks[IND_BLOCK]);
    inode->blocks[IND_BLOCK] = 0;
    return 0;
}

// Traverse path from root ("/") to get parent directory inode or final inode
int traverse(const char *path, struct wfs_inode *result_inode, int *result_inum) {
    if (strcmp(path, "/") == 0) {
        struct wfs_inode root_inode;
        load_inode(0, &root_inode);
        if (result_inode) {
            *result_inode = root_inode;
        }
        if (result_inum) {
            *result_inum = 0;
        }
        return 0;
    }

    size_t path_len = strlen(path);
    char *working_path = (char *)malloc(path_len + 1);
    if (!working_path) {
        return -ENOMEM;
    }
    strcpy(working_path, path);

    struct wfs_inode current_dir;
    load_inode(0, &current_dir);
    int current_inode_num = 0;

    size_t token_start = 1; // Skip the leading '/'
    while (token_start < path_len) {
        size_t token_end = token_start;
        while (token_end < path_len && working_path[token_end] != '/') {
            token_end++;
        }

        working_path[token_end] = '\0';
        const char *token = &working_path[token_start];

        struct wfs_dentry directory_entry;
        if (find_dentry(&current_dir, token, &directory_entry) < 0) {
            free(working_path);
            return -ENOENT;
        }

        load_inode(directory_entry.num, &current_dir);
        current_inode_num = directory_entry.num;

        token_start = token_end + 1;
    }

    if (result_inode) {
        *result_inode = current_dir;
    }
    if (result_inum) {
        *result_inum = current_inode_num;
    }
    free(working_path);
    return 0;
}

// /* ================================== UNLINK ============================== */
int wfs_unlink(const char *path) {
    fprintf(stderr, "[DEBUG] wfs_unlink: Initiating unlink for '%s'\n", path);

    char *dir_copy = strdup(path);
    char *base_copy = strdup(path);

    char *dir_name = dirname(dir_copy);
    char *base_name = basename(base_copy);

    fprintf(stderr, "[DEBUG] wfs_unlink: Identified parent directory='%s', target name='%s'\n", dir_name, base_name);

    struct wfs_inode parent_inode;
    int parent_inum;
    if (traverse(dir_name, &parent_inode, &parent_inum) != 0) {
        fprintf(stderr, "[DEBUG] wfs_unlink: Unable to find parent directory '%s'\n", dir_name);
        return -ENOENT;
    }
    fprintf(stderr, "[DEBUG] wfs_unlink: Retrieved parent inode, number=%d\n", parent_inum);

    struct wfs_dentry dentry;
    if (find_dentry(&parent_inode, base_name, &dentry) != 0) {
        fprintf(stderr, "[DEBUG] wfs_unlink: File '%s' not located within '%s'\n", base_name, dir_name);
        return -ENOENT;
    }
    fprintf(stderr, "[DEBUG] wfs_unlink: Located target inode, number=%d\n", dentry.num);

    struct wfs_inode target_inode;
    if (load_inode(dentry.num, &target_inode) != 0) {
        fprintf(stderr, "[DEBUG] wfs_unlink: Failed to load target inode %d\n", dentry.num);
        return -EIO;
    }

    fprintf(stderr, "[DEBUG] wfs_unlink: Proceeding to remove directory entry '%s'\n", base_name);

    if (remove_dentry_by_name(&parent_inode, base_name) != 0) {
        fprintf(stderr, "[DEBUG] wfs_unlink: Removal of directory entry '%s' failed\n", base_name);
        return -ENOENT;
    }

    fprintf(stderr, "[DEBUG] wfs_unlink: Releasing all blocks linked to inode %d\n", dentry.num);

    for (int i = 0; i < D_BLOCK; i++) {
        if (target_inode.blocks[i] != 0) {
            free_data_block_all_disks(target_inode.blocks[i]);
            target_inode.blocks[i] = 0;
        }
    }

    free_indirect_blocks(&target_inode);

    struct wfs_sb *superb = (struct wfs_sb *)disk_img[0];
    uint32_t *data_bitmap = (uint32_t *)((char *)disk_img[0] + superb->d_bitmap_ptr);
    memset(data_bitmap, 0, (superb->num_data_blocks / 8));

    for (int i = 0; i < D_BLOCK; i++) {
        target_inode.blocks[i] = 0;
    }

    free_inode_all_disks(target_inode.num);

    if (find_dentry(&parent_inode, base_name, &dentry) == 0) {
        dentry.num = 0;
    }

    parent_inode.mtim = parent_inode.ctim = time(NULL);
    fprintf(stderr, "[DEBUG] wfs_unlink: Parent inode times updated (mtim and ctim)\n");
    store_inode(parent_inum, &parent_inode);
    fprintf(stderr, "[DEBUG] wfs_unlink: Parent inode changes successfully stored\n");
    fprintf(stderr, "[DEBUG] wfs_unlink: Unlink operation for '%s' completed successfully\n", path);
    return 0;
}

// /* ================================== RMDIR =============================== */
int wfs_rmdir(const char *path) {
    printf("[DEBUG] wfs_rmdir \n");
    // just call wfs_unlink, same logic if empty directory
    return wfs_unlink(path);
}

/* ================================== READ ================================ */
int wfs_read(const char *path, char *buf, size_t length, off_t offset, struct fuse_file_info *fi) {
    printf("[DEBUG] enter wfs_read \n");

    size_t read_size = 0;
    size_t position = offset;
    struct wfs_inode *inode;
    char *path_copy = strdup(path);

    if (lookup_inode_by_path(path_copy, &inode, 0) < 0) {
        free(path_copy);
        return -1;
    }

    if (raid_mode == 0 || raid_mode == 1) {
        // RAID 0 or RAID 1: Single disk read
        int disk = 0;
        while (read_size < length && position < inode->size) {
            size_t start = BLOCK_SIZE - (position % BLOCK_SIZE);
            size_t end = inode->size - position;
            if (start > end) start = end;

            char *addr = calculate_block_offset(inode, position, 0, disk);
            memcpy(buf + read_size, addr, start);

            position += start;
            read_size += start;
        }
    } else {
        // RAID 1V: Read from multiple disks and verify
        int chsums[MAX_DISKS] = {0};
        int best_disk = -1;
        int max_count = -1;

        for (int i = 0; i < num_disks; i++) {
            memset(buf, 0, length);
            size_t temp_num_bytes = 0;
            size_t temp_position = offset;

            while (temp_num_bytes < length && temp_position < inode->size) {
                size_t start = BLOCK_SIZE - (temp_position % BLOCK_SIZE);
                size_t end = inode->size - temp_position;
                if (start > end) {
                    start = end;
                }

                char *addr = calculate_block_offset(inode, temp_position, 0, i);
                memcpy(buf + temp_num_bytes, addr, start);

                temp_position += start;
                temp_num_bytes += start;
            }

            for (size_t s = 0; s < temp_num_bytes; s++) {
                chsums[i] += buf[s];
            }
        }

        for (int i = 0; i < num_disks; i++) {
            if (chsums[i] == -1) continue;

            int count = 1;
            for (int j = i + 1; j < num_disks; j++) {
                if (chsums[i] == chsums[j]) {
                    chsums[j] = -1;
                    count++;
                }
            }

            if (count > max_count) {
                max_count = count;
                best_disk = i;
            }
        }

        if (best_disk != -1) {
            read_size = 0;
            position = offset;
            while (read_size < length && position < inode->size) {
                size_t start = BLOCK_SIZE - (position % BLOCK_SIZE);
                size_t end = inode->size - position;
                if (start > end) {
                    start = end;
                }

                char *addr = calculate_block_offset(inode, position, 0, best_disk);
                memcpy(buf + read_size, addr, start);

                position += start;
                read_size += start;
            }
        }
    }

    free(path_copy);
    return read_size;
}

/* ================================= WRITE ================================ */
int wfs_write(const char *path, const char *buf, size_t length, off_t offset, struct fuse_file_info *fi) {
    printf("[DEBUG] wfs_write \n");

    int ret;
    for (int i = 0; i < (raid_mode == 0 ? 1 : num_disks); i++) {
        struct wfs_inode *inode;
        char *path_copy = strdup(path);

        if (lookup_inode_by_path(path_copy, &inode, i) < 0) {
            free(path_copy);
            return -ENOENT;
        }

        ssize_t new_data_len = length - (inode->size - offset);

        size_t written_bytes = 0;
        size_t position = offset;

        while (written_bytes < length) {
            size_t to_write = BLOCK_SIZE - (position % BLOCK_SIZE);

            if (to_write + written_bytes > length) {
                to_write = length - written_bytes;
            }

            char *addr = calculate_block_offset(inode, position, 1, i);
            if (addr == NULL) {
                fprintf(stderr, "Failed to calculate data block offset.\n");
                free(path_copy);
                return -ENOENT;
            }

            memcpy(addr, buf + written_bytes, to_write);

            position += to_write;
            written_bytes += to_write;
        }

        if (new_data_len > 0) {
            inode->size += new_data_len;
        }

        if (raid_mode == 0) {
            for (int i = 0; i < num_disks; i++) {
                struct wfs_inode *wfs_inode = get_inode_by_number(inode->num, i);
                wfs_inode->size = inode->size;
            }
        }
        free(path_copy);
        ret = written_bytes;
    }

    return ret;
}

/* ================================ READDIR =============================== */
int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    printf("[DEBUG] enter wfs_readdir\n");

    for (int i = 1; i <= 2; i++) {
        char dot_input[3];
        memset(dot_input, '.', i);
        dot_input[i] = '\0';

        filler(buf, dot_input, NULL, 0);
    }

    struct wfs_inode *inode;
    char *path_copy = strdup(path);
    if (lookup_inode_by_path(path_copy, &inode, 0) < 0) {
        free(path_copy);
        return -ENOENT;
    }

    int inc_size = sizeof(struct wfs_dentry);
    struct wfs_dentry *dentries;
    for (off_t i = 0; i < inode->size; i += inc_size) {
        dentries = (struct wfs_dentry *)calculate_block_offset(inode, i, 0, 0);
        if (dentries->num != 0) filler(buf, dentries->name, NULL, 0);
    }

    free(path_copy);
    return 0;
}

/* ================================= MAIN ================================= */
static struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .mknod = wfs_mknod,
    .mkdir = wfs_mkdir,
    .unlink = wfs_unlink,
    .rmdir = wfs_rmdir,
    .read = wfs_read,
    .write = wfs_write,
    .readdir = wfs_readdir,
};

int main(int argc, char *argv[]) {
    // validation
    if (argc <= 3) {
        fprintf(stderr, "Usage: %s disk1 disk2 [FUSE options] mount_point\n", argv[0]);
        return -1;
    }

    while (num_disks < argc - 1 && argv[num_disks + 1][0] != '-') num_disks++;
 
    if (num_disks == 0 || num_disks < 2 || num_disks > MAX_DISKS) {
        fprintf(stderr, "Disk error.\n");
        return -1;
    }

    int fds[MAX_DISKS];
    struct stat file_stat;
    for (int i = 0; i < num_disks; i++) {
        char *disk_image_path = argv[i + 1];
        fds[i] = open(disk_image_path, O_RDWR, 0666);
        if (fds[i] == -1) {
            perror("open");
            return -1;
        }

        if (fstat(fds[i], &file_stat) == -1) {
            perror("stat");
            return -1;
        }
    }

    // map memory
    for (int i = 0; i < num_disks; i++) {
        disk_img[i] = mmap(NULL, file_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fds[i], 0);
        if (disk_img[i] == MAP_FAILED) {
            perror("mmap");
            return -1;
        }
    }

    // order superblock
    struct wfs_sb *superb = (struct wfs_sb *)disk_img[0];
    disk_order[superb->disk_id] = 0;

    // check consistency
    for (int i = 1; i < num_disks; i++) {
        struct wfs_sb *other = (struct wfs_sb *)disk_img[i];

        if (superb->tim != other->tim || superb->raid_mode != other->raid_mode || memcmp(superb, other, 48)) return -1;
        disk_order[other->disk_id] = i;
    }

    // order disks
    void *tmp[MAX_DISKS];
    for (int i = 0; i < num_disks; ++i) {
        tmp[disk_order[i]] = disk_img[i];
    }
    memcpy(disk_img, tmp, num_disks * sizeof(void*));

    // fuse arguments
    char **fuse_argv = malloc((argc - num_disks) * sizeof(char *));
    if (!fuse_argv) {
        perror("malloc");
        return -1;
    }

    fuse_argv[0] = argv[0];

    for (int i = num_disks + 1; i < argc; i++) {
        fuse_argv[i - num_disks] = argv[i];
    }
    int fuse_argc = argc - num_disks;

    raid_mode = superb->raid_mode;

    // start FUSE
    printf("[DEBUG] main: starting fuse_main\n");
    int ret = fuse_main(fuse_argc, fuse_argv, &ops, NULL);

    // cleanup
    for (int i = 0; i < num_disks; i++) {
        munmap(disk_img[i], file_stat.st_size);
        close(fds[i]);
    }

    free(fuse_argv);
    return ret;
}