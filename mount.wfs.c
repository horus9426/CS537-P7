#define FUSE_USE_VERSION 30
#include <errno.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>

char *fs;

static int wfs_getattr(const char *path, struct stat *stbuf)
{
    
    return 0;
    
}

static int wfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    return 0;
}

static int wfs_mkdir(const char *path, mode_t mode)
{
    return 0;
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{
    return 0;
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{
    return 0;
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *info)
{
    return 0;
}

static int wfs_unlink(const char *path)
{
    return 0;
}

static struct fuse_operations my_operations = {
    .getattr	= wfs_getattr,
    .mknod      = wfs_mknod,
    .mkdir      = wfs_mkdir,
    .read	= wfs_read,
    .write      = wfs_write,
    .readdir	= wfs_readdir,
    .unlink    	= wfs_unlink,
};

int main(int argc, char *argv[])
{
    int ret;
   
   
    ret = fuse_main(argc, argv, &my_operations, NULL);
   
    return ret;
}
