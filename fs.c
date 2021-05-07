
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

int allocate_new_block(int blocks)
{
    for (int i = 0; i < blocks; i++)
    {
        if (!bitmap[i])
        {
            bitmap[i] = 1;
            return i;
        }
    }

    return 0;
}
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
                // Check if the whole block is valid
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
    // Check to see if a disk is mounted
    if (bitmap == NULL)
    {
        return 0;
    }

    union fs_block block;
    disk_read(0, block.data);

    int free_inode = 0;

    for (int k = 1; k <= block.super.ninodeblocks; k++)
    {
        // Set inode block to valid when creating
        if (bitmap[k] == 0) {
            bitmap[k] == 1;
        }

        // Read inode block
        disk_read(k, block.data);
        struct fs_inode inode;

        // Traverse each inode in the inode block
        for (int j = 1; j < INODES_PER_BLOCK; j++)
        {
            inode = block.inode[j];

            // If inode is invalid. insert valid inode
            if (!inode.isvalid)
            {
                free_inode = (k - 1) * INODES_PER_BLOCK + j;

                inode.isvalid = 1;
                inode.size = 0;

                // Setting direct pointers to 0
                for (int i; i < POINTERS_PER_INODE; i++)
                {
                    inode.direct[i] = 0;
                }
                
                //memset(inode.direct, 0, sizeof(inode.direct));
                
                inode.indirect = 0;

                //bitmap[j] = 1;

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
    // Check to see if a disk is mounted
    if (!fs_mounted)
    {
        return 0;
    }

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
    // Check to see if a filesystem is mounted
    if (!fs_mounted)
    {
        return 0;
    }

    // Check to see if a valid inumber is passed
    if (inumber <= 0)
    {
        return 0;
    }

    int pointer_count, is_direct_block, bytes_left, bytes_read = 0;

    union fs_block block, indirect_block;
    struct fs_inode inode;

    char loop_data[4096] = "";
    char total_data[16384] = "";

    // Determine the inode offset as well as the inode block and pointer offset
    int inode_offset = inumber % INODES_PER_BLOCK;
    int block_index = inumber / INODES_PER_BLOCK + 1;
    int pointer_offset = offset / 4096;

    // Read from the inodes block
    disk_read(block_index, block.data);
    inode = block.inode[inode_offset];
    int inode_size = inode.size;

    // Check to make sure inode is valid and has a reasonable size
    if ((!inode.isvalid) || !inode_size)
    {
        return 0;
    }

    // Determine how many bytes can/need to be read
    if ((inode_size - offset) < length)
    {
        bytes_left = inode_size - offset;
    }
    else
    {
        bytes_left = length;
    }

    // Traverse through each direct pointer in the inode
    for (int i = pointer_offset; i < POINTERS_PER_INODE; i++)
    {
        is_direct_block = inode.direct[i];

        // If direct block exists, read a piece of data and copy it. Recalculate how much has been read
        if (is_direct_block)
        {
            disk_read(is_direct_block, *(&loop_data));
            strcat(*(&total_data), *(&loop_data));

            if ((bytes_left - bytes_read) < 4096)
            {
                bytes_read += bytes_left - bytes_read;
            }
            else
            {
                bytes_read += 4096;
            }

            if (bytes_read >= bytes_left)
            {
                strcpy(data, total_data);
                return bytes_read;
            }
        }
    }

    // Traverse through each indirect pointer in the inode if it exists
    if (inode.indirect)
    {
        disk_read(inode.indirect, indirect_block.data);

        if (pointer_offset < 5)
        {
            pointer_count = 0;
        }
        else
        {
            pointer_count = pointer_offset - 5;
        }

        // Start looking from pointer offset
        for (int j = pointer_count; j < POINTERS_PER_BLOCK; j++)
        {
            if (indirect_block.pointers[j])
            {
                disk_read(indirect_block.pointers[j], *(&loop_data));
                strcat(*(&total_data), *(&loop_data));

                if ((bytes_left - bytes_read) < 4096)
                {
                    bytes_read += bytes_left - bytes_read;
                }
                else
                {
                    bytes_read += 4096;
                }

                if (bytes_read >= bytes_left)
                {
                    strcpy(data, total_data);
                    return bytes_read;
                }
            }
        }
    }

    return bytes_read;
}

int fs_write(int inumber, const char *data, int length, int offset)
{
    // Check to see if a filesystem is mounted
    if (!fs_mounted)
    {
        return 0;
    }

    // Check to see if a valid inumber is passed
    if (inumber <= 0)
    {
        return 0;
    }

    int pointer_count, new_block, bytes_left, bytes_written = 0;
    union fs_block block, indirect_block, super_block;

    char total_data[16384] = "";
    strcpy(total_data, data);

    disk_read(0, super_block.data);

    // Determine the inode offset as well as the inode block and pointer offset
    int inode_offset = inumber % INODES_PER_BLOCK;
    int block_index = inumber / INODES_PER_BLOCK + 1;
    int pointer_offset = offset / 4096;

    // Read from the inodes block
    disk_read(block_index, block.data);

    int inode_size = (POINTERS_PER_INODE + POINTERS_PER_BLOCK) * 4096;

    // Determine how many bytes can/need to be written
    if ((inode_size - offset) < length)
    {
        bytes_left = inode_size - offset;
    }
    else
    {
        bytes_left = length;
    }

    // Check if inode is valid
    if (!block.inode[inode_offset].isvalid)
    {
        return 0;
    }

    // If the offset is 0 at the start, reset direct and indirect pointers to 0
    if (offset == 0)
    {
        //Iterate through pointers of direct block
        for (int x = 0; x < POINTERS_PER_INODE; x++)
        {
            if (block.inode[inode_offset].direct[x] <= 0)
            {
                continue;
            }

            bitmap[block.inode[inode_offset].direct[x]] = 0;
            block.inode[inode_offset].direct[x] = 0;
        }

        if (block.inode[inode_offset].indirect > 0)
        {
            union fs_block indirect_block;
            disk_read(block.inode[inode_offset].indirect, indirect_block.data);

            // Iterate through pointers of indirect block
            for (int y = 0; y < POINTERS_PER_BLOCK; y++)
            {
                if (indirect_block.pointers[y] <= 0)
                {
                    continue;
                }

                bitmap[indirect_block.pointers[y]] = 0;
                indirect_block.pointers[y] = 0;
            }

            disk_write(block.inode[inode_offset].indirect, indirect_block.data);

            bitmap[block.inode[inode_offset].indirect] = 0;
            block.inode[inode_offset].indirect = 0;
        }

        disk_write(block_index, block.data);
    }

    // Traverse through each direct pointer in the inode
    for (int i = pointer_offset; i < POINTERS_PER_INODE; i++)
    {
        new_block = allocate_new_block(super_block.super.nblocks);

        // If the disk is full, there are no more blocks left
        if (!new_block)
        {
            block.inode[inode_offset].size = offset + bytes_written;
            disk_write(block_index, block.data);

            return bytes_written;
        }

        block.inode[inode_offset].direct[i] = new_block;
        disk_write(block.inode[inode_offset].direct[i], total_data);

        // Write a piece of data and copy it. Recalculate how much has been read
        if ((bytes_left - bytes_written) < 4096)
        {
            bytes_written += bytes_left - bytes_written;
        }
        else
        {
            bytes_written += 4096;
        }

        strcpy(total_data, &data[bytes_written]);

        // Check to see if too many bytes were written
        if (bytes_written >= bytes_left)
        {
            block.inode[inode_offset].size = offset + bytes_written;
            disk_write(block_index, block.data);

            return bytes_written;
        }
    }

    // Check if you need to create an indirect block
    if (!block.inode[inode_offset].indirect)
    {
        block.inode[inode_offset].indirect = allocate_new_block(super_block.super.nblocks);
        disk_write(block.inode[inode_offset].indirect, indirect_block.data);
    }

    // Traverse through all the pointers in the indirect block
    if (pointer_offset < 5)
    {
        pointer_count = 0;
    }
    else
    {
        pointer_count = pointer_offset - 5;
    }

    for (int j = pointer_count; j < POINTERS_PER_BLOCK; j++)
    {
        new_block = allocate_new_block(super_block.super.nblocks);

        // If the disk is full, there are no more blocks left
        if (!new_block)
        {
            block.inode[inode_offset].size = offset + bytes_written;
            disk_write(block.inode[inode_offset].indirect, indirect_block.data);

            disk_write(block_index, block.data);

            return bytes_written;
        }

        indirect_block.pointers[j] = new_block;
        disk_write(indirect_block.pointers[j], total_data);

        // Write a piece of data and copy it. Recalculate how much has been read
        if ((bytes_left - bytes_written) < 4096)
        {
            bytes_written += bytes_left - bytes_written;
        }
        else
        {
            bytes_written += 4096;
        }

        strcpy(total_data, &data[bytes_written]);

        // Check to see if too many bytes were written
        if (bytes_written >= bytes_left)
        {
            block.inode[inode_offset].size = offset + bytes_written;
            disk_write(block.inode[inode_offset].indirect, indirect_block.data);

            disk_write(block_index, block.data);

            // Iterate through pointers of indirect block
            for (int i = 0; i < POINTERS_PER_BLOCK; i++)
            {
                if (indirect_block.pointers[i] <= 0)
                {
                    continue;
                }
            }

            return bytes_written;
        }
    }

    return bytes_written;
}