/*
 * A Very Very Simple Filesystem
 * Eric McCreath 2006, 2008, 2010 - GPL
 *
 * (based on the simplistic RAM filesystem McCreath 2001)
 */

/* to make use:
 *    make -C /usr/src/linux-headers-2.6.32-23-generic/  SUBDIRS=$PWD modules
 * (or just make, with the accompanying Makefile)
 *
 * to load use:
 *    sudo insmod vvsfs.ko
 * (may need to copy vvsfs.ko to a local filesystem first)
 *
 * to make a suitable filesystem:
 *    dd of=myvvsfs.raw if=/dev/zero bs=512 count=100
 *    ./mkfs.vvsfs myvvsfs.raw
 * (could also use a USB device etc.)
 *
 * to mount use:
 *    mkdir testdir
 *    sudo mount -o loop -t vvsfs myvvsfs.raw testdir
 *
 * to use a USB device:
 *    create a suitable partition on USB device (exercise for reader)
 *        ./mkfs.vvsfs /dev/sdXn
 *    where sdXn is the device name of the usb drive
 *    mkdir testdir
 *    sudo mount -t vvsfs /dev/sdXn testdir
 *
 * use the file system:
 *    cd testdir
 *    echo hello > file1
 *    cat file1
 *    cd ..
 *
 * unmount the filesystem:
 *    sudo umount testdir
 *
 * remove the module:
 *    sudo rmmod vvsfs
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/statfs.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <asm/uaccess.h>
#include <linux/seq_file.h>

#include "vvsfs.h"

#define DEBUG 1

static struct inode_operations vvsfs_file_inode_operations;
static struct file_operations vvsfs_file_operations;
static struct inode_operations vvsfs_dir_inode_operations;
static struct file_operations vvsfs_dir_operations;
static struct super_operations vvsfs_ops;

struct inode *vvsfs_iget(struct super_block *sb, unsigned long ino);

struct vvsfs_info {
    int size;
    int block_count;
};

static struct vvsfs_info vvsfs_info;

static void vvsfs_put_super(struct super_block *sb)
{
    if (DEBUG)
        printk("vvsfs - put_super\n");
    return;
}

static int vvsfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
    if (DEBUG)
        printk("vvsfs - statfs\n");

    buf->f_namelen = MAXNAME;
    return 0;
}

// vvsfs_readblock - reads a block from the block device (this will copy over
//                   the top of inode)
static int vvsfs_readblock(struct super_block *sb,
                           int inum,
                           struct vvsfs_inode *inode)
{
    struct buffer_head *bh;

    if (DEBUG)
        printk("vvsfs - readblock : %d\n", inum);

    bh = sb_bread(sb, inum);
    memcpy((void *)inode, (void *)bh->b_data, BLOCKSIZE);
    brelse(bh);
    if (DEBUG)
        printk("vvsfs - readblock done : %d\n", inum);
    return BLOCKSIZE;
}

// vvsfs_writeblock - write a block from the block device(this will just mark the block
//                    as dirtycopy)
static int vvsfs_writeblock(struct super_block *sb,
                            int inum,
                            struct vvsfs_inode *inode)
{
    struct buffer_head *bh;

    if (DEBUG)
        printk("vvsfs - writeblock : %d\n", inum);

    bh = sb_bread(sb, inum);
    memcpy(bh->b_data, inode, BLOCKSIZE);
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    if (DEBUG)
        printk("vvsfs - writeblock done: %d\n", inum);
    return BLOCKSIZE;
}

// vvsfs_readdir - reads a directory and places the result using filldir

static int
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
vvsfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
#else
vvsfs_readdir(struct file *filp, struct dir_context *ctx)
#endif
{
    struct inode *i;
    struct vvsfs_inode dirdata;
    int num_dirs;
    struct vvsfs_dir_entry *dent;
    int error, k;

    if (DEBUG)
        printk("vvsfs - readdir\n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
    i = filp->f_dentry->d_inode;
#else
    i = file_inode(filp);
#endif
    vvsfs_readblock(i->i_sb, i->i_ino, &dirdata);
    num_dirs = dirdata.size / sizeof(struct vvsfs_dir_entry);

    if (DEBUG)
        printk("Number of entries %d fpos %Ld\n", num_dirs, filp->f_pos);

    error = 0;
    k = 0;
    dent = (struct vvsfs_dir_entry *)&dirdata.data;
    while (!error && filp->f_pos < dirdata.size && k < num_dirs)
    {
        printk("adding name : %s ino : %d\n", dent->name, dent->inode_number);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
        error = filldir(dirent,
                        dent->name, strlen(dent->name), filp->f_pos, dent->inode_number, DT_REG);
        if (error)
            break;
        filp->f_pos += sizeof(struct vvsfs_dir_entry);
#else
        if (dent->inode_number)
        {
            if (!dir_emit(ctx, dent->name, strnlen(dent->name, MAXNAME),
                          dent->inode_number, DT_UNKNOWN))
                return 0;
        }
        ctx->pos += sizeof(struct vvsfs_dir_entry);
#endif
        k++;
        dent++;
    }
    // update_atime(i);
    printk("done readdir\n");

    return 0;
}

// vvsfs_lookup - A directory name in a directory. It basically attaches the inode
//                of the file to the directory entry.
static struct dentry *vvsfs_lookup(struct inode *dir,
                                   struct dentry *dentry,
                                   unsigned int flags)
{
    int num_dirs;
    int k;
    struct vvsfs_inode dirdata;
    struct inode *inode = NULL;
    struct vvsfs_dir_entry *dent;

    if (DEBUG)
        printk("vvsfs - lookup\n");

    vvsfs_readblock(dir->i_sb, dir->i_ino, &dirdata);
    num_dirs = dirdata.size / sizeof(struct vvsfs_dir_entry);

    for (k = 0; k < num_dirs; k++)
    {
        dent = (struct vvsfs_dir_entry *)((dirdata.data) + k * sizeof(struct vvsfs_dir_entry));

        if ((strlen(dent->name) == dentry->d_name.len) &&
            strncmp(dent->name, dentry->d_name.name, dentry->d_name.len) == 0)
        {
            inode = vvsfs_iget(dir->i_sb, dent->inode_number);

            if (!inode)
                return ERR_PTR(-EACCES);

            d_add(dentry, inode);
            return NULL;
        }
    }
    d_add(dentry, inode);
    return NULL;
}

// vvsfs_empty_inode - finds the first free inode (returns -1 is unable to find one)
static int vvsfs_empty_inode(struct super_block *sb)
{
    struct vvsfs_inode block;
    int k;
    for (k = 0; k < NUMBLOCKS; k++)
    {
        vvsfs_readblock(sb, k, &block);
        if (block.is_empty)
            return k;
    }
    return -1;
}

// vvsfs_new_inode - find and construct a new inode.
struct inode * vvsfs_new_inode(const struct inode * dir, umode_t mode, int is_dir)
{
    struct vvsfs_inode block;
    struct super_block *sb;
    struct inode *inode;
    int newinodenumber;

    if (DEBUG)
        printk("vvsfs - new inode\n");

    if (!dir)
        return NULL;
    sb = dir->i_sb;

    /* get an vfs inode */
    inode = new_inode(sb);
    if (!inode)
        return NULL;

    inode_init_owner(inode, dir, mode);
    /* find a spare inode in the vvsfs */
    newinodenumber = vvsfs_empty_inode(sb);
    if (newinodenumber == -1)
    {
        printk("vvsfs - inode table is full.\n");
        return NULL;
    }

    block.is_empty = false;
    block.size = 0;
    block.is_directory = is_dir;
    block.i_mode = mode;
    block.i_uid = 0;
    block.i_gid = 0;

    vvsfs_writeblock(sb,newinodenumber,&block);
    if (DEBUG) printk("vvsfs - new inode finish writing\n");

    inode_init_owner(inode, dir, mode);
    inode->i_ino = newinodenumber;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
    inode->i_ctime = inode->i_mtime = inode->i_atime = CURRENT_TIME;
#else
    inode->i_ctime = inode->i_mtime = inode->i_atime = current_time(inode);
#endif

    inode->i_op = NULL; //
    inode->i_uid.val = (uid_t) 0;
    inode->i_gid.val = (gid_t) 0;

    insert_inode_hash(inode);
    // printk("vvsfs - new inode: size + : %i\n", inode->i_size);
    vvsfs_info.size += inode->i_size;
    vvsfs_info.block_count++;
    return inode;
}

static int vvsfs_unlink(struct inode *dir, struct dentry *dentry){
    int num_dirs;
    int k;
    struct vvsfs_inode dirdata;
    struct inode *inode = NULL;
    struct vvsfs_dir_entry *dent;
    //
    struct vvsfs_inode filedata;

    vvsfs_readblock(dir->i_sb,dir->i_ino,&dirdata);
    num_dirs = dirdata.size/sizeof(struct vvsfs_dir_entry);

    for (k=0;k < num_dirs;k++) {
        dent = (struct vvsfs_dir_entry *) ((dirdata.data) + k*sizeof(struct vvsfs_dir_entry));

        if ((strlen(dent->name) == dentry->d_name.len) &&
                strncmp(dent->name,dentry->d_name.name,dentry->d_name.len) == 0) {
            inode = vvsfs_iget(dir->i_sb, dent->inode_number);

            if (!inode)
                return -EACCES;

            memmove(dent, dent + sizeof(struct vvsfs_dir_entry), (num_dirs - k - 1) * sizeof(struct vvsfs_dir_entry));
            dirdata.size = (num_dirs - 1) * sizeof(struct vvsfs_dir_entry);
            vvsfs_writeblock(dir->i_sb, dir->i_ino, &dirdata);
            dir->i_size = dirdata.size;
            mark_inode_dirty(dir);
            inode->i_ctime = dir->i_ctime;
            inode_dec_link_count(inode); // has mark dirty
            if (inode->i_nlink == 0)
            {
                vvsfs_readblock(inode->i_sb, inode->i_ino, &filedata);
                // printk("vvsfs - unlink: info - %i\n", filedata.size);
                vvsfs_info.size -= filedata.size;
                vvsfs_info.block_count--;
                filedata.is_empty = 1;
                filedata.is_directory = 0;
                filedata.size = 0;
                vvsfs_writeblock(inode->i_sb, inode->i_ino, &filedata);
                mark_inode_dirty(inode);
            }
            return 0;
        }
    }
    return -ENOENT;
}

// vvsfs_setattr - set attr for an inode
static int vvsfs_setattr(struct dentry *dentry, struct iattr *attr){
    struct inode *inode = d_inode(dentry);
    int error;
    //
    struct vvsfs_inode filedata;
    int k;

    error = setattr_prepare(dentry, attr);
    if (error)
        return error;
    printk("vvsfs - setattr : %s\n", dentry->d_name.name);

    if ((attr->ia_valid & ATTR_SIZE) &&
        attr->ia_size != i_size_read(inode))
    {
        error = inode_newsize_ok(inode, attr->ia_size);
        if (error)
            return error;

        // return error when size is larger then max file size
        if (attr->ia_size > MAXFILESIZE)
        {
            return -1;
        }
        int size_o = inode->i_size;
        printk("vvsfs - setattr try to set size: %ld\n", inode->i_ino);
        truncate_setsize(inode, attr->ia_size);
        printk("vvsfs - setattr try to set size: done");
        // vvsfs_truncate(inode);
        vvsfs_readblock(inode->i_sb, inode->i_ino, &filedata);

        if (filedata.size < attr->ia_size)
        {
            for (k = filedata.size; k < attr->ia_size; k++)
            {
                filedata.data[k] = '\0';
            }
        }
        // printk("vvsfs - setattr: size_o %i, filedata size %i, attr size %i\n", size_o, filedata.size, attr->ia_size);
        vvsfs_info.size += attr->ia_size - size_o;
        filedata.size = attr->ia_size; // int  // check if is directory
        vvsfs_writeblock(inode->i_sb, inode->i_ino, &filedata);
    }

    setattr_copy(inode, attr);
    mark_inode_dirty(inode);

    vvsfs_readblock(inode->i_sb,inode->i_ino,&filedata);
    if (attr->ia_valid & ATTR_UID)
		filedata.i_uid = inode->i_uid.val;
	if (attr->ia_valid & ATTR_GID)
		filedata.i_gid = inode->i_gid.val;
    if (attr->ia_valid & ATTR_MODE)
        filedata.i_mode = inode->i_mode;
    vvsfs_writeblock(inode->i_sb,inode->i_ino,&filedata);

    return 0;
}

// vvsfs_create - create a new directory in a directory
static int vvsfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
    struct vvsfs_inode dirdata;
    int num_dirs;
    struct vvsfs_dir_entry *dent;
    // struct vvsfs_inode new_dir;

    struct inode * inode;

    if (DEBUG) printk("vvsfs - mkdir : %s\n",dentry->d_name.name);

    inode = vvsfs_new_inode(dir, mode|S_IFDIR, 1);  // S_IRUGO|S_IWUGO|S_IXUGO|S_IFDIR;
    // printk("vvsfs - mkdir : %d\n", S_ISDIR(inode->i_mode));
    if (!inode)
        return -ENOSPC;
    inode->i_op = &vvsfs_dir_inode_operations; 
    inode->i_fop = &vvsfs_dir_operations;
    // inode->i_mode = mode;  // NEED CHANGE

    /* get an vfs inode */
    if (!dir) return -1;

    vvsfs_readblock(dir->i_sb,dir->i_ino,&dirdata);
    num_dirs = dirdata.size/sizeof(struct vvsfs_dir_entry);
    dent = (struct vvsfs_dir_entry *) ((dirdata.data) +
                                       num_dirs*sizeof(struct vvsfs_dir_entry));

    strncpy(dent->name, dentry->d_name.name,dentry->d_name.len);
    dent->name[dentry->d_name.len] = '\0';


    dirdata.size = (num_dirs + 1) * sizeof(struct vvsfs_dir_entry);

    // update i_size
    dir->i_size = dirdata.size;
    
    //


    dent->inode_number = inode->i_ino;


    vvsfs_writeblock(dir->i_sb,dir->i_ino,&dirdata);
    
    // 
    inode_inc_link_count(dir);
    inode_inc_link_count(inode);
    mark_inode_dirty(dir);
    mark_inode_dirty(inode);

    d_instantiate(dentry, inode);

    printk("Dir created %ld\n",inode->i_ino);
    return 0;
}

static int vvsfs_rmdir(struct inode * dir, struct dentry *dentry){
    struct inode * inode = d_inode(dentry);
	int err = -ENOTEMPTY;
    struct vvsfs_inode dirdata;
    vvsfs_readblock(inode->i_sb,inode->i_ino,&dirdata);

	if (dirdata.size == 0) {
		err = vvsfs_unlink(dir, dentry);
		if (!err) {
			inode_dec_link_count(dir);
			inode_dec_link_count(inode);
            if (inode->i_nlink==0){   
                    if (DEBUG) printk("vvsfs - rmdir : %s\n", dentry->d_name.name);                 
                    dirdata.is_empty = 1;
                    dirdata.is_directory = 0;
                    dirdata.size = 0;
                    vvsfs_writeblock(inode->i_sb,inode->i_ino,&dirdata);
                    mark_inode_dirty(inode);
            }

		}
	}
	return err;
}

// vvsfs_mknod - create a device special file 
static int vvsfs_mknod(struct inode * dir, struct dentry *dentry, umode_t mode, dev_t rdev){
    struct vvsfs_inode dirdata;
    int num_dirs;
    struct vvsfs_dir_entry *dent;

    struct inode * inode;

    if (DEBUG) printk("vvsfs - mknod : %s\n",dentry->d_name.name);

    // inode = vvsfs_new_inode(dir, S_IRUGO|S_IWUGO|S_IXUGO|mode, 0);  // S_IFDIR
    // if (DEBUG) printk("vvsfs - mknod finish creating new inode\n");
    // if (!inode)
    //     return -ENOSPC;
    // init_special_inode(inode, inode->i_mode, rdev);
    inode = vvsfs_new_inode(dir, S_IRUGO|S_IWUGO|S_IFREG, 0);  // S_IFDIR
    if (!inode)
        return -ENOSPC;
    inode->i_op = &vvsfs_file_inode_operations; 
    inode->i_fop = &vvsfs_file_operations;
    inode->i_mode = mode; // NO NEED
    init_special_inode(inode, inode->i_mode, rdev);

    if (DEBUG) printk("vvsfs - mknod finish setting\n");

    /* get an vfs inode */
    if (!dir) return -1;

    vvsfs_readblock(dir->i_sb,dir->i_ino,&dirdata);
    num_dirs = dirdata.size/sizeof(struct vvsfs_dir_entry);
    dent = (struct vvsfs_dir_entry *) ((dirdata.data) +
                                       num_dirs*sizeof(struct vvsfs_dir_entry));

    strncpy(dent->name, dentry->d_name.name,dentry->d_name.len);
    dent->name[dentry->d_name.len] = '\0';


    dirdata.size = (num_dirs + 1) * sizeof(struct vvsfs_dir_entry);

    // update i_size
    dir->i_size = dirdata.size;
    
    //


    dent->inode_number = inode->i_ino;


    vvsfs_writeblock(dir->i_sb,dir->i_ino,&dirdata);
    if (DEBUG) printk("vvsfs - mknod finish writing to dir: %ld\n", inode->i_ino);
    
    // 
    mark_inode_dirty(dir);
    mark_inode_dirty(inode);
    if (DEBUG) printk("vvsfs - mknod finish making dirty dentry: %s\n", dentry->d_name.name);

    d_instantiate(dentry, inode);

    printk("Node created %ld\n",inode->i_ino);
    return 0;
}

// vvsfs_create - create a new file in a directory 
static int vvsfs_create(struct inode *dir,
                        struct dentry *dentry,
                        umode_t mode,
                        bool excl)
{
    struct vvsfs_inode dirdata;
    int num_dirs;
    struct vvsfs_dir_entry *dent;

    struct inode *inode;

    if (DEBUG)
        printk("vvsfs - create : %s\n", dentry->d_name.name);

    inode = vvsfs_new_inode(dir, mode|S_IFREG, 0);  // S_IRUGO|S_IWUGO|S_IFREG
    if (!inode)
        return -ENOSPC;
    inode->i_op = &vvsfs_file_inode_operations; 
    inode->i_fop = &vvsfs_file_operations;
    // inode->i_mode = mode|S_IFREG; // NO NEED

    /* get an vfs inode */
    if (!dir)
        return -1;

    vvsfs_readblock(dir->i_sb, dir->i_ino, &dirdata);
    num_dirs = dirdata.size / sizeof(struct vvsfs_dir_entry);
    dent = (struct vvsfs_dir_entry *)((dirdata.data) +
                                      num_dirs * sizeof(struct vvsfs_dir_entry));

    strncpy(dent->name, dentry->d_name.name, dentry->d_name.len);
    dent->name[dentry->d_name.len] = '\0';

    dirdata.size = (num_dirs + 1) * sizeof(struct vvsfs_dir_entry);

    // update i_size
    dir->i_size = dirdata.size;
    
    //


    dent->inode_number = inode->i_ino;

    vvsfs_writeblock(dir->i_sb, dir->i_ino, &dirdata);

    //
    mark_inode_dirty(dir);
    mark_inode_dirty(inode);

    d_instantiate(dentry, inode);

    printk("File created %ld\n", inode->i_ino);
    return 0;
}

// vvsfs_file_write - write to a file
static ssize_t vvsfs_file_write(struct file *filp,
                                const char *buf,
                                size_t count,
                                loff_t *ppos)
{
    struct vvsfs_inode filedata;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
    struct inode *inode = filp->f_dentry->d_inode;
#else
    struct inode *inode = filp->f_path.dentry->d_inode;
#endif
    ssize_t pos;
    struct super_block *sb;
    char *p;

    if (DEBUG)
        printk("vvsfs - file write - count : %zu ppos %Ld\n",
               count, *ppos);

    if (!inode)
    {
        printk("vvsfs - Problem with file inode\n");
        return -EINVAL;
    }

    if (!(S_ISREG(inode->i_mode)))
    {
        printk("vvsfs - not regular file\n");
        return -EINVAL;
    }
    if (*ppos > inode->i_size || count <= 0)
    {
        printk("vvsfs - attempting to write over the end of a file.\n");
        return 0;
    }
    sb = inode->i_sb;

    vvsfs_readblock(sb, inode->i_ino, &filedata);

    if (filp->f_flags & O_APPEND)
        pos = inode->i_size;
    else
        pos = *ppos;

    if (pos + count > MAXFILESIZE)
        return -ENOSPC;

    filedata.size = pos + count;
    p = filedata.data + pos;
    if (copy_from_user(p, buf, count))
        return -ENOSPC;
    *ppos = pos;
    buf += count;

    vvsfs_info.size += count;
    inode->i_size = filedata.size;
    inode->i_mode = filedata.i_mode;
    vvsfs_writeblock(sb, inode->i_ino, &filedata);

    if (DEBUG)
        printk("vvsfs - file write done : %zu ppos %Ld\n", count, *ppos);

    return count;
}

// vvsfs_file_read - read data from a file
static ssize_t vvsfs_file_read(struct file *filp,
                               char *buf,
                               size_t count,
                               loff_t *ppos)
{
    struct vvsfs_inode filedata;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
    struct inode *inode = filp->f_dentry->d_inode;
#else
    struct inode *inode = filp->f_path.dentry->d_inode;
#endif
    char *start;
    ssize_t offset, size;

    struct super_block *sb;

    if (DEBUG)
        printk("vvsfs - file read - count : %zu ppos %Ld\n", count, *ppos);

    if (!inode)
    {
        printk("vvsfs - Problem with file inode\n");
        return -EINVAL;
    }

    if (!(S_ISREG(inode->i_mode)))
    {
        printk("vvsfs - not regular file\n");
        return -EINVAL;
    }
    if (*ppos > inode->i_size || count <= 0)
    {
        printk("vvsfs - attempting to write over the end of a file.\n");
        return 0;
    }
    sb = inode->i_sb;

    printk("r : readblock\n");
    vvsfs_readblock(sb, inode->i_ino, &filedata);

    start = buf;
    printk("rr\n");
    size = MIN(inode->i_size - *ppos, count);

    printk("readblock : %zu\n", size);
    offset = *ppos;
    *ppos += size;

    printk("r copy_to_user\n");

    if (copy_to_user(buf, filedata.data + offset, size))
        return -EIO;
    buf += size;

    printk("r return\n");
    return size;
}

static struct file_operations vvsfs_file_operations =
{
    read:   vvsfs_file_read,        /* read */
    write:  vvsfs_file_write,       /* write */
    // mmap:   generic_file_mmap,
};

static struct inode_operations vvsfs_file_inode_operations = {
    .setattr    = vvsfs_setattr,
};

static struct file_operations vvsfs_dir_operations =
    {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
        .readdir = vvsfs_readdir, /* readdir */
#else
        .llseek = generic_file_llseek,
        .read = generic_read_dir,
        .iterate = vvsfs_readdir,
        .fsync = generic_file_fsync,
#endif
};

static struct inode_operations vvsfs_dir_inode_operations = {
    create: vvsfs_create,           /* create */
    unlink: vvsfs_unlink,
    mkdir: vvsfs_mkdir,
    rmdir: vvsfs_rmdir,
    mknod: vvsfs_mknod,
    setattr: vvsfs_setattr,
    lookup: vvsfs_lookup,           /* lookup */
};

// vvsfs_iget - get the inode from the super block
struct inode *vvsfs_iget(struct super_block *sb, unsigned long ino)
{
    struct inode *inode;
    struct vvsfs_inode filedata;

    if (DEBUG)
    {
        printk("vvsfs - iget - ino : %d", (unsigned int)ino);
        printk(" super %p\n", sb);
    }

    inode = iget_locked(sb, ino);
    if (!inode)
        return ERR_PTR(-ENOMEM);
    if (!(inode->i_state & I_NEW))
        return inode;

    vvsfs_readblock(inode->i_sb, inode->i_ino, &filedata);
    i_uid_write(inode, filedata.i_uid);
    i_gid_write(inode, filedata.i_gid);
    inode->i_mode = filedata.i_mode;

    inode->i_size = filedata.size;

    i_uid_write(inode, filedata.i_uid); 
    i_gid_write(inode, filedata.i_gid); 
    inode->i_mode = filedata.i_mode;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
    inode->i_ctime = inode->i_mtime = inode->i_atime = CURRENT_TIME;
#else
    inode->i_ctime = inode->i_mtime = inode->i_atime = current_time(inode);
#endif

    if (filedata.is_directory)
    {
        // inode->i_mode = S_IRUGO|S_IWUGO|S_IFDIR;
        inode->i_op = &vvsfs_dir_inode_operations;
        inode->i_fop = &vvsfs_dir_operations;
    }
    else
    {
        // inode->i_mode = S_IRUGO|S_IWUGO|S_IFREG;
        inode->i_op = &vvsfs_file_inode_operations;
        inode->i_fop = &vvsfs_file_operations;
    }

    unlock_new_inode(inode);
    return inode;
}

// vvsfs_fill_super - read the super block (this is simple as we do not
//                    have one in this file system)
static int vvsfs_fill_super(struct super_block *s, void *data, int silent)
{
    struct inode *i;
    int hblock;
    //
    struct vvsfs_inode dirdata;


    if (DEBUG)
        printk("vvsfs - fill super\n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
    s->s_flags = MS_NOSUID | MS_NOEXEC;
#else
    s->s_flags = ST_NOSUID | SB_NOEXEC;
#endif
    s->s_op = &vvsfs_ops;

    i = new_inode(s);

    i->i_sb = s;
    i->i_ino = 0;
    i->i_flags = 0;
    // i->i_mode = S_IRUGO|S_IWUGO|S_IXUGO|S_IFDIR;
    i->i_op = &vvsfs_dir_inode_operations;
    i->i_fop = &vvsfs_dir_operations;

    vvsfs_readblock(i->i_sb,i->i_ino,&dirdata);
    i_uid_write(i, dirdata.i_uid); 
    i_gid_write(i, dirdata.i_gid); 
    i->i_mode = dirdata.i_mode;

    printk("inode %p\n", i);

    hblock = bdev_logical_block_size(s->s_bdev);
    if (hblock > BLOCKSIZE)
    {
        printk("device blocks are too small!!");
        return -1;
    }

    set_blocksize(s->s_bdev, BLOCKSIZE);
    s->s_blocksize = BLOCKSIZE;
    s->s_blocksize_bits = BLOCKSIZE_BITS;
    s->s_root = d_make_root(i);

    return 0;
}

static struct super_operations vvsfs_ops =
    {
        statfs : vvsfs_statfs,
        put_super : vvsfs_put_super,
    };

static struct dentry *vvsfs_mount(struct file_system_type *fs_type,
                                  int flags,
                                  const char *dev_name,
                                  void *data)
{
    return mount_bdev(fs_type, flags, dev_name, data, vvsfs_fill_super);
}

static struct file_system_type vvsfs_type =
    {
        .owner = THIS_MODULE,
        .name = "vvsfs",
        .mount = vvsfs_mount,
        .kill_sb = kill_block_super,
        .fs_flags = FS_REQUIRES_DEV,
};

static int vvsfs_proc_show(struct seq_file *m, void *v)
{
    seq_printf(m, "Size: %i, Count: %i\n", vvsfs_info.size, vvsfs_info.block_count);
    return 0;
}

static int vvsfs_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, vvsfs_proc_show, NULL);
}

static const struct file_operations vvsfs_proc_fops = {
    .open = vvsfs_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release};

static int __init vvsfs_init(void)
{
    vvsfs_info.block_count = 0;
    vvsfs_info.size = 0;
    printk("Registering vvsfs\n");
    proc_create("vvsfs", 0, NULL, &vvsfs_proc_fops);
    return register_filesystem(&vvsfs_type);
}

static void __exit vvsfs_exit(void)
{
    printk("Unregistering the vvsfs.\n");
    remove_proc_entry("vvsfs", NULL);
    unregister_filesystem(&vvsfs_type);
}

module_init(vvsfs_init);
module_exit(vvsfs_exit);
MODULE_LICENSE("GPL");
