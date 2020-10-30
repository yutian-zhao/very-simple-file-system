# 1 Page Summary

In this assignment, we have achieved the following things:
*  remove and truncate files
*  create and remove directories
*  store user id, group id and permissions (including root, directories and files)
*  add device nodes
*  make file executable (only small files, like short sripts but not binary program)
*  enable mmap functionality (enable the default mmap operation)
*  Add a proc entry to inform users of the number of inodes and the amount memory the filesystem is currently using.

The assignment is done by 2 people collaboratively, most tasks are more or less distributed by both. Overall, the task distribution is as follows:
*  Yutian Zhao (u6489809): remove and truncate files, create and remove directories, fix to hold permission after remounting, add device node.
*  Hong Wang (u6535557): enable storing permissions, add a proc entry, debug and polish the code.

## Remove files

vvsfs_unlink directory node operation. By removing a file, the inode should be reset and the dentry should be deleted.
In order to achieve this, first we need to look up the inode by the dentry, decrease the link count and if 0, clean the inode.

## Truncate files

vvsfs_setattr file operation. By truncating, it can extend a file with "/0"s, shrink a file by cutting its tail
or create a new one if no exist. It's not allowed to truncate a file to exceed the maximum file size.

## create directories

vvsfs_mkdir directory node operation. It's similar to create files, except: it should be marked as directory, its mode should be directory, 
the operation for it should be directory operations.

## remove direcotries
vvsfs_rmdir directory node operation.  It's similar to remove files, except it checks if it's empty.

## store user id, group id and permissions
vvsfs_setattr file operation and vvsfs_iget operation. The permission information is stored in block device and loaded to inode. When changes,
setattr operation will set permission for inode and we store the changed permission back to the device. We load the permission info in iget operation so that
everytime mounting the file system the permission is persist.

## add device nodes
vvsfs_mknod file operation. This operation enables our file system to support sepcial device like char/block device. Our design is that the
device node should not be stored in the disk and should disappear after unmounting. Thus we create inode and add it to dentry but not store it in the disk.
The key thing here is to call init_special_inode, which will associate the file with special operations, which will forward the operation to the 
device driver. We test it on the special system driver like /dev/zeros and ramp device driver we wrote for lab 5. We can read and write to it to change its content.

## enable mmap
generic_file_mmap. We don't put much effort on this one, thus use the default one.

## make file executable
We don't put much effort on this one. This should be achieved when the permission modification is successful. 
It can't run binary programs as it doesn't have enough space.

## proc entry


