/*
 *  mkfs.vvsfs - constructs an initial empty file system
 * Eric McCreath 2006 GPL
 *
 * To compile :
 *     gcc mkfs.vvsfs.c -o mkfs.vvsfs
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
// #include <linux/stat.h>

#include "vvsfs.h"

char* device_name;
int device;

static void die(char *mess)
{
    fprintf(stderr,"Exit : %s\n",mess);
    exit(1);
}

static void usage(void)
{
    die("Usage : mkfs.vvsfs <device name>)");
}

int main(int argc, char ** argv)
{
    int k;

    if (argc != 2) usage();

    // open the device for reading and writing
    device_name = argv[1];
    device = open(device_name,O_RDWR);

    off_t pos=0;
    struct vvsfs_inode inode;

    int i;
    for (i = 0; i < NUMBLOCKS; i++)
    {  // write each of the blocks
        printf("writing : %d\n",i);
        if (i == 0)
        {  // the first block is an empty directory
            inode.is_empty = 0;
            inode.is_directory = 1;
            inode.i_mode = 0777|S_IFDIR;
        }
        else
        {
            inode.is_empty = 1;
            inode.is_directory = 0;
            inode.i_mode = 0;
        }
        inode.i_gid = 0;
        inode.i_uid = 0;
        inode.size = 0;
        for (k = 0;k< MAXFILESIZE;k++)
            inode.data[k] = 0;

         // move the file pointer to the correct block
        if (pos != lseek(device,pos,SEEK_SET))
            die("seek set failed");
         // write the block
        if (sizeof(struct vvsfs_inode) !=
                write(device,&inode,sizeof(struct vvsfs_inode)))
            die("inode write failed");
        pos += sizeof(struct vvsfs_inode);
    }

    close(device);
    return 0;
}

