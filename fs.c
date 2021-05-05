
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
int *bitmap;
int new_blocks;

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

void create_new_bitmap()
{

    union fs_block block;

    union fs_block indirect_block;

    // Begin processing of the new bitmap
    for (int i = 0; i < disk_size(); i++)
    {
        // Read info for new filesystem
        disk_read(i, block.data);

        if (!i)
        { // Check if superblock and set it
            if (block.super.magic == FS_MAGIC)
            {
                bitmap[0] = 1;
            }
            else
            {
                bitmap[0] = 0;
            }
        }
        else if (i <= new_blocks)
        { // Set up inode blocks
            // Check for validity of inode
            int valid = 0;

            // Go through each inode to check for bitmap
            for (int j = 0; j < INODES_PER_BLOCK; j++)
            {
                // Check if the who;e block is valid
                if (block.inode[j].isvalid)
                {
                    bitmap[i] = 1;
                    valid = 1;

                    // Check the direct pointers
                    for (int k = 0; k < POINTERS_PER_INODE; k++)
                    {
                        if (block.inode[j].direct[k])
                        {
                            bitmap[block.inode[j].direct[k]] = 1;
                        }
                    }

                    // Check the indirect pointer
                    if (block.inode[j].indirect)
                    {
                        // Read the indirect pointer and check the indirect block
                        disk_read(block.inode[j].indirect, indirect_block.data);

                        // Check pointers on indirect block
                        for (int l = 0; l < POINTERS_PER_BLOCK; l++)
                        {
                            if (indirect_block.pointers[l])
                            {
                                bitmap[indirect_block.pointers[l]] = 1;
                            }
                        }
                    }
                }
            }

            // If whole block is not valid, mark it as invalid
            if (!valid)
            {
                bitmap[i] = 0;
            }
        }
    }
}

void print_inode(struct fs_inode *current_inode, int inode_block, int block_offset)
{
    // Counter for number of direct blocks
    int direct_blocks = 0;

    // Print the inode number and size
    printf("inode %d:\n", (inode_block - 1) * INODES_PER_BLOCK + block_offset);
    printf("    size: %d bytes\n", current_inode->size);

    // Search for direct blocks to report
    for (int i = 0; i < POINTERS_PER_INODE; i++)
    {
        // If direct block is found, report information
        if (current_inode->direct[i])
        {
            direct_blocks++;

            if (direct_blocks == 1)
            {
                printf("    direct blocks:");
            }

            // Lists direct blocks
            printf(" %d", current_inode->direct[i]);
        }

        // Determine if more spacing is needed
        if (i == POINTERS_PER_INODE - 1 && direct_blocks)
        {
            printf("\n");
        }
    }

    // Checks if inode has an indirect block
    if (current_inode->indirect)
    {
        union fs_block indirect_block;

        // Reads information about indirect block
        disk_read(current_inode->indirect, indirect_block.data);

        // Print information about indirect block
        printf("    indirect block: %d\n", current_inode->indirect);
        printf("    indirect data blocks:");

        for (int j = 0; j < POINTERS_PER_BLOCK; j++)
        {
            // Lists indirect data blocks
            if (indirect_block.pointers[j])
            {
                printf(" %d", indirect_block.pointers[j]);
            }
        }

        printf("\n");
    }
}

int set_inode_blocks()
{
    int inode_blocks;

    // Ensure that at least ten percent of the blocks are reserved for inodes
    if (disk_size() % 10 == 0)
    {
        inode_blocks = disk_size() / 10;
    }
    else
    {
        inode_blocks = (disk_size() / 10) + 1;
    }

    return inode_blocks;
}

void destroy_data(int inode_blocks)
{
    union fs_block block;

    for (int i = 1; i <= inode_blocks; i++)
    {
        // Read inode block
        disk_read(i, block.data);

        // Traverse each inode in the inode block
        for (int j = 0; j < INODES_PER_BLOCK; j++)
        {
            // Make each inode invalid
            block.inode[j].isvalid = 0;
        }

        // Write the destroyed inode block back to the disk
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
    for (int i = 1; i <= block.super.ninodeblocks; i++)
    {
        // Read inode block
        disk_read(i, block.data);

        // Traverse each inode in the inode block
        for (int j = 0; j < INODES_PER_BLOCK; j++)
        {
            // If inode is valid (has info), print it out
            if (block.inode[j].isvalid)
            {
                print_inode(&(block.inode[j]), i, j);
            }
        }
    }
}

int fs_format()
{
    // If filesystem is already mounted, return an error
    if (fs_mounted)
    {
        return 0;
    }

    union fs_block block;

    // Create each element of the super block
    block.super.magic = FS_MAGIC;
    block.super.nblocks = disk_size();
    block.super.ninodeblocks = set_inode_blocks();
    block.super.ninodes = block.super.ninodeblocks * INODES_PER_BLOCK;

    // Destroy any existing data in the filesystem
    destroy_data(block.super.ninodeblocks);
    disk_write(0, block.data);

    // Formatted successfully
    return 1;
}

int fs_mount()
{
    union fs_block block;
    disk_read(0, block.data);

    // Cannot mount on top of an another filesystem
    if (block.super.magic != FS_MAGIC)
    {
        return 0;
    }

    // Allocate space for the new free block bitmap
    bitmap = calloc(block.super.nblocks, sizeof(int));

    // Allocation failed
    if (!bitmap)
    {
        return 0;
    }

    // Set new values for the system and mark as mounted
    new_blocks = block.super.ninodeblocks;
    fs_mounted = 1;

    // Creates new free block bitmap
    create_new_bitmap();

    // Mounted successfully
    return 1;
}

int fs_create()
{

    union fs_block block;
    disk_read(0, block.data);

    int free_inode = 0;

    for (int k = 1; k <= block.super.ninodeblocks; k++)
    {
        // Read inode block
        disk_read(k, block.data);

        // Traverse each inode in the inode block
        for (int j = 1; j < INODES_PER_BLOCK; j++)
        {
            // If inode is invalid. insert valid inode
            if (!block.inode[j].isvalid)
            {
                free_inode = (k - 1) * INODES_PER_BLOCK + j;

                struct fs_inode inode;

                inode.isvalid = 1;

                inode.size = 0; // size??

                for (int i; i < POINTERS_PER_INODE; i++)
                {
                    inode.direct[i] = 0;
                }

                inode.indirect = 0;

                block.inode[j] = inode;

                disk_write(k, block.data);
                break;
            }
        }
    }
    return free_inode;
}

int fs_delete(int inumber)
{
    union fs_block super_block;

    disk_read(0, super_block.data);

    if (inumber > super_block.super.ninodes || inumber <= 0)
    {
        return 0;
    }

    union fs_block block;
    // Read inode block
    disk_read(((inumber / INODES_PER_BLOCK) + 1), block.data);

    // if inode doesn't exist, return 0
    if (!block.inode[inumber % INODES_PER_BLOCK].isvalid)
    {
        return 0;
    }

    // for each direct block mapping, if it has a value, free it
    for (int i = 0; i < POINTERS_PER_INODE; i++)
    {

        if (block.inode[inumber % INODES_PER_BLOCK].direct[i])
        {
            bitmap[block.inode[inumber % INODES_PER_BLOCK].direct[i]] = 1;
            block.inode[inumber % INODES_PER_BLOCK].direct[i] = 0;
        }
    }
    // if there is an indirect block mapping, free it
    if (block.inode[inumber % INODES_PER_BLOCK].indirect)
    {
        union fs_block indirect_block;
        disk_read(block.inode[inumber % INODES_PER_BLOCK].indirect, indirect_block.data);

        for (int i = 0; i < POINTERS_PER_INODE; i++)
        {
            if (indirect_block.inode[inumber % INODES_PER_BLOCK].direct[i])
            {
                bitmap[indirect_block.inode[inumber % INODES_PER_BLOCK].direct[i]] = 1;
                indirect_block.inode[inumber % INODES_PER_BLOCK].direct[i] = 0;
            }
        }
        disk_write(block.inode[inumber % INODES_PER_BLOCK].indirect, indirect_block.data);
    }

    // set valid bit to 0
    block.inode[inumber % INODES_PER_BLOCK].isvalid = 0;
    // write
    disk_write(((inumber / INODES_PER_BLOCK) + 1), block.data);

    return 1;
}

int fs_getsize(int inumber)
{
    union fs_block block;
    disk_read(0, block.data);

    // Check if inumber is 0, which is an invalid inumber.
    if (inumber <= 0)
    {
        return -1;
    }
    // Find correct inode block
    int index = (inumber + INODES_PER_BLOCK) / INODES_PER_BLOCK;

    // Check if inode block is in limits
    if (index > block.super.ninodeblocks)
    {
        return -1;
    }

    // Read inode from inode block
    disk_read(index, block.data);
    struct fs_inode inode = block.inode[inumber % INODES_PER_BLOCK];

    // Check if valid inode; if inode is valid, return the size
    if (inode.isvalid)
    {
        return inode.size;
    }

    // Inode was invalid and return error
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