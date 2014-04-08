/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  Based on fusexmp_fs.c

  gcc -Wall `pkg-config fuse --cflags --libs` -lulockmgr simplecowfs.c simplecow.c -o simplecowfs
*/

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE

#include <fuse.h>
#include <ulockmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <semaphore.h>
#include "simplecow.h"


struct myinfo {
    int fd;
    sem_t sem;
    struct simplecow* cow; // copy on write
};

static int simplecowfs_getattr(const char *path, struct stat *stbuf)
{
	int res;

	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int simplecowfs_fgetattr(const char *path, struct stat *stbuf,
			struct fuse_file_info *fi)
{
	int res;

	(void) path;
	
	struct myinfo* i = (struct myinfo*)(uintptr_t)  fi->fh;
	if(!i) return -ENOSYS;
	
	res = fstat(i->fd, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int simplecowfs_access(const char *path, int mask)
{
	int res;

	res = access(path, (mask&~0222));
	if (res == -1)
		return -errno;

	return 0;
}

static int simplecowfs_readlink(const char *path, char *buf, size_t size)
{
	int res;

	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}

struct simplecowfs_dirp {
	DIR *dp;
	struct dirent *entry;
	off_t offset;
};

static int simplecowfs_opendir(const char *path, struct fuse_file_info *fi)
{
	int res;
	struct simplecowfs_dirp *d = malloc(sizeof(struct simplecowfs_dirp));
	if (d == NULL)
		return -ENOMEM;

	d->dp = opendir(path);
	if (d->dp == NULL) {
		res = -errno;
		free(d);
		return res;
	}
	d->offset = 0;
	d->entry = NULL;

	fi->fh = (unsigned long) d;
	return 0;
}

static inline struct simplecowfs_dirp *get_dirp(struct fuse_file_info *fi)
{
	return (struct simplecowfs_dirp *) (uintptr_t) fi->fh;
}

static int simplecowfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	struct simplecowfs_dirp *d = get_dirp(fi);

	(void) path;
	if (offset != d->offset) {
		seekdir(d->dp, offset);
		d->entry = NULL;
		d->offset = offset;
	}
	while (1) {
		struct stat st;
		off_t nextoff;

		if (!d->entry) {
			d->entry = readdir(d->dp);
			if (!d->entry)
				break;
		}

		memset(&st, 0, sizeof(st));
		st.st_ino = d->entry->d_ino;
		st.st_mode = d->entry->d_type << 12;
		nextoff = telldir(d->dp);
		if (filler(buf, d->entry->d_name, &st, nextoff))
			break;

		d->entry = NULL;
		d->offset = nextoff;
	}

	return 0;
}

static int simplecowfs_releasedir(const char *path, struct fuse_file_info *fi)
{
	struct simplecowfs_dirp *d = get_dirp(fi);
	(void) path;
	closedir(d->dp);
	free(d);
	return 0;
}

static int simplecowfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	return -EACCES;
}

static int simplecowfs_mkdir(const char *path, mode_t mode)
{
	return -EACCES;
}

static int simplecowfs_unlink(const char *path)
{
	return -EACCES;
}

static int simplecowfs_rmdir(const char *path)
{
	return -EACCES;
}

static int simplecowfs_symlink(const char *from, const char *to)
{
	return -EACCES;
}

static int simplecowfs_rename(const char *from, const char *to)
{
	return -EACCES;
}

static int simplecowfs_link(const char *from, const char *to)
{
	return -EACCES;
}

static int simplecowfs_chmod(const char *path, mode_t mode)
{
	return -EACCES;
}

static int simplecowfs_chown(const char *path, uid_t uid, gid_t gid)
{
	return -EACCES;
}

static int simplecowfs_truncate(const char *path, off_t size)
{
	return -EACCES;
}

static int simplecowfs_ftruncate(const char *path, off_t size,
			 struct fuse_file_info *fi)
{
	return -EACCES;
}

static int simplecowfs_utimens(const char *path, const struct timespec ts[2])
{
	return -EACCES;
}

static int simplecowfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	return -EACCES;
}

static int backing_read(void* usr, long long int off, int size, char* b);

static int simplecowfs_open(const char *path, struct fuse_file_info *fi)
{
	int fd;

	fd = open(path, (fi->flags & (~O_RDWR) & (~O_WRONLY)) | O_RDONLY);
	if (fd == -1)
		return -errno;

	struct myinfo* i = (struct myinfo*)malloc(sizeof *i);
	if(!i) {
		close(fd);
		return -ENOMEM;
	}
	i->fd = fd;
    sem_init(&i->sem, 0, 1);
    
	i->cow = simplecow_create(&backing_read, (void*)i);
    
    fi->fh = (uintptr_t)  i;

	return 0;
}

int backing_read(void* usr, long long int off, int size, char* b) {
	struct myinfo* i  = (struct myinfo*)usr;
	
	int res;

	res = pread(i->fd, b, size, off);
	if (res == -1)
		res = -errno;

	return res;
}

static int simplecowfs_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	(void) path;
	struct myinfo* i = (struct myinfo*)(uintptr_t)  fi->fh;
	if(!i) return -ENOSYS;
	
    sem_wait(&i->sem);
	
	int ret = simplecow_read(i->cow, offset, size, buf);
	
    sem_post(&i->sem);
	return ret;
}

static int simplecowfs_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	struct myinfo* i = (struct myinfo*)(uintptr_t)  fi->fh;
	if(!i) return -ENOSYS;
	
    sem_wait(&i->sem);
	
	int ret = simplecow_write(i->cow, offset, size, buf);
	
    sem_post(&i->sem);
	return ret;
}

static int simplecowfs_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int simplecowfs_flush(const char *path, struct fuse_file_info *fi)
{
	(void)path;
	(void)fi;
	return 0;
}

static int simplecowfs_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	
	struct myinfo* i = (struct myinfo*)(uintptr_t)  fi->fh;
	if(!i) return -ENOSYS;
	
	simplecow_destroy(i->cow);
	sem_destroy(&i->sem);

	return 0;
}

static int simplecowfs_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	return 0;
}

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int simplecowfs_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	return -EACCES;
}

static int simplecowfs_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int simplecowfs_listxattr(const char *path, char *list, size_t size)
{
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int simplecowfs_removexattr(const char *path, const char *name)
{
	return -EACCES;
}
#endif /* HAVE_SETXATTR */

static int simplecowfs_lock(const char *path, struct fuse_file_info *fi, int cmd,
		    struct flock *lock)
{
	(void) path;
	
	struct myinfo* i = (struct myinfo*)(uintptr_t)  fi->fh;
	if(!i) return -ENOSYS;

	return ulockmgr_op(i->fd, cmd, lock, &fi->lock_owner,
			   sizeof(fi->lock_owner));
}

static struct fuse_operations simplecowfs_oper = {
	.getattr	= simplecowfs_getattr,
	.fgetattr	= simplecowfs_fgetattr,
	.access		= simplecowfs_access,
	.readlink	= simplecowfs_readlink,
	.opendir	= simplecowfs_opendir,
	.readdir	= simplecowfs_readdir,
	.releasedir	= simplecowfs_releasedir,
	.mknod		= simplecowfs_mknod,
	.mkdir		= simplecowfs_mkdir,
	.symlink	= simplecowfs_symlink,
	.unlink		= simplecowfs_unlink,
	.rmdir		= simplecowfs_rmdir,
	.rename		= simplecowfs_rename,
	.link		= simplecowfs_link,
	.chmod		= simplecowfs_chmod,
	.chown		= simplecowfs_chown,
	.truncate	= simplecowfs_truncate,
	.ftruncate	= simplecowfs_ftruncate,
	.utimens	= simplecowfs_utimens,
	.create		= simplecowfs_create,
	.open		= simplecowfs_open,
	.read		= simplecowfs_read,
	.write		= simplecowfs_write,
	.statfs		= simplecowfs_statfs,
	.flush		= simplecowfs_flush,
	.release	= simplecowfs_release,
	.fsync		= simplecowfs_fsync,
#ifdef HAVE_SETXATTR
	.setxattr	= simplecowfs_setxattr,
	.getxattr	= simplecowfs_getxattr,
	.listxattr	= simplecowfs_listxattr,
	.removexattr	= simplecowfs_removexattr,
#endif
	.lock		= simplecowfs_lock,

	.flag_nullpath_ok = 1,
};

int main(int argc, char *argv[])
{
	umask(0);
	return fuse_main(argc, argv, &simplecowfs_oper, NULL);
}
