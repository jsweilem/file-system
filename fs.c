
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define DISK_BLOCK_SIZE 4096
#define FS_MAGIC 0xf0f03410
#define INODES_PER_BLOCK 128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

int fs_mounted = 0;

struct fs_superblock
{
    int magic;
    int nblocks;
    int ninodeblocks;
    int ninodes;
};

struct fs_inode
{
    int isvalid;
    int size;
    int direct[POINTERS_PER_INODE];
    int indirect;
};

union fs_block
{
    struct fs_superblock super;
    struct fs_inode inode[INODES_PER_BLOCK];
    int pointers[POINTERS_PER_BLOCK];
    char data[DISK_BLOCK_SIZE];
};

void print_inode(struct fs_inode *current_inode, int inode_block, int block_offset) {
    // Counter for number of direct blocks
    int direct_blocks = 0;

    // Print the inode number and size
    printf("inode %d:\n", (inode_block - 1) * INODES_PER_BLOCK + block_offset);
    printf("    size: %d bytes\n", current_inode->size);

    // Search for direct blocks to report
    for (int i = 0; i < POINTERS_PER_INODE; i++) {
        // If direct block is found, report information
        if (current_inode->direct[i]) {
            direct_blocks++;

            if (direct_blocks == 1) {
                printf("    direct blocks:");
            }
            
            // Lists direct blocks
            printf(" %d", current_inode->direct[i]);
        }

        // Determine if more spacing is needed
        if (i == POINTERS_PER_INODE - 1 && direct_blocks) {
            printf("\n");
        }
    }

    // Checks if inode has an indirect block
    if (current_inode->indirect) {
        union fs_block indirect_block;

        // Reads information about indirect block
		disk_read(current_inode->indirect, indirect_block.data);

        // Print information about indirect block
        printf("    indirect block: %d\n", current_inode->indirect);
		printf("    indirect data blocks:");

        for (int j = 0; j < POINTERS_PER_BLOCK; j++) {
            // Lists indirect data blocks
            if (indirect_block.pointers[j]) {
                printf(" %d", indirect_block.pointers[j]);
            }
        }

        printf("\n");
    }
}

int set_inode_blocks() {
    int inode_blocks;
    
    // Ensure that at least ten percent of the blocks are reserved for inodes
    if (disk_size() % 10 == 0) {
        inode_blocks = disk_size() / 10;
    }
    else {
        inode_blocks = (disk_size() / 10) + 1;
    }

    return inode_blocks;
}

void destroy_data(int inode_blocks) {  
    union fs_block block;
    
    for (int i = 1; i <= inode_blocks; i++) {
        // Read inode block
        disk_read(i, block.data);

        // Traverse each inode in the inode block
        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            // Make each inode invalid
            block.inode[j].isvalid = 0;
        }

        // Write the destroyed inode block back to the filesystem
        disk_write(i, block.data);
    }
}

void fs_debug()
{
    union fs_block block;

    disk_read(0, block.data);

    printf("superblock:\n");
    printf("    %d blocks\n", block.super.nblocks);
    printf("    %d inode blocks\n", block.super.ninodeblocks);
    printf("    %d inodes\n", block.super.ninodes);

    // Traverse each inode block
    for (int i = 1; i <= block.super.ninodeblocks; i++) {
        // Read inode block
        disk_read(i, block.data);

        // Traverse each inode in the inode block
        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            // If inode is valid (has info), print it out
            if (block.inode[j].isvalid) {
                print_inode(&(block.inode[j]), i, j);
            }
        }
    }
}

int fs_format()
{
    // If filesystem is already mounted, return an error
    if (fs_mounted) {
        return 0;
    }

    // Create new filesystem block
    union fs_block block;

    // Create each element of the super block
    block.super.magic = FS_MAGIC;
    block.super.nblocks = disk_size();
    block.super.ninodeblocks = set_inode_blocks();
    block.super.ninodes = block.super.ninodeblocks * INODES_PER_BLOCK;

    // Destroy any existing data in the filesystem
    destroy_data(block.super.ninodeblocks);
    disk_write(0, block.data);

    // Created successfully
    return 1;
}

int fs_mount()
{
    return 0;
}

int fs_create()
{
    return 0;
}

int fs_delete(int inumber)
{
    return 0;
}

int fs_getsize(int inumber)
{
    return -1;
}

int fs_read(int inumber, char *data, int length, int offset)
{
    return 0;
}

int fs_write(int inumber, const char *data, int length, int offset)
{
    return 0;
}