simplecowfs allows turning read-only files to read-write ones my storing small amount of changes in memory.

Usage
---

    # simplecowfs /mnt/simplecow
    # mount -t reiserfs -o loop /mnt/simplecow/mnt/storage/reiserfs_dirty_image.dat /mnt/tmp
    
Changes are preserved until the file is closed.

Note that no clever data structure is used. Overhead of each read operation is O(n) where n is number of previous write operations.
