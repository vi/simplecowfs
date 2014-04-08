simplecowfs allows you to turn read-only files into read-write ones by storing small amount of changes in memory.
Useful for mounting read-only images with filesystems with unreplayed journal.

Usage
---

    # simplecowfs /mnt/simplecow
    # mount -t reiserfs -o loop /mnt/simplecow/mnt/storage/reiserfs_dirty_image.dat /mnt/tmp
    
Changes are preserved until the file is closed.

Note that no clever data structure is used. Overhead of each read operation is O(n) where n is number of previous write operations.

Bonus: holdopenfs
---

`holdopenfs` -- Proxy filesystem that accumulates and reuses file descriptors.
You can issue multiple command line operations referring to one file on `holdopenfs` and second and subsequent times the file will not be re-opened.

```
$ holdopenfs m2
$ wc -l m2/etc/passwd
127 m2/etc/passwd
# chmod 000 /etc/passwd
$ wc -l /etc/passwd
wc: /etc/passwd: Permission denied
$ wc -l m2/etc/passwd
127 m2/etc/passwd
$ ls -l /proc/$(pidof holdopenfs)/fd/4
lr-x------ 1 vi vi 64 Apr  8 05:11 /proc/16651/fd/4 -> /etc/passwd
# chmod 644 /etc/passwd
```
