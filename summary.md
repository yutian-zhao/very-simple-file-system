# VVSFS


### proc entry
User can get the total file size, file and directory count from `/proc/vvsfs` entry. However, it has limits that the data is not stored and will lose when the module is removed. The information is stored in the local variable. It changed when `vvsfs_new_inode`, `vvsfs_unlink`, `vvsfs_setattr`, `vvsfs_mkdir`, `vvsfs_rmdir`, `vvsfs_create`, `vvsfs_file_write` are called.
```
$ mkdir obfish
$ cat /proc/vvsfs
File Size: 0, File Count: 0, Directory Count: 1
$ mkdir fish
$ cat /proc/vvsfs
File Size: 0, File Count: 0, Directory Count: 2
$ echo ob > ob
$ cat /proc/vvsfs
File Size: 3, File Count: 1, Directory Count: 2
```