#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "wfs.h"

char *file_buf;

void write_superblock()
{
    struct wfs_sb superblock;
    superblock.magic = WFS_MAGIC;
    superblock.head = 0;
    memmove(file_buf, &superblock, sizeof(superblock));
   
}

void write_log_entry()
{
    unsigned int creation_time = time(NULL);
    struct wfs_dentry first_dir;
    
    struct wfs_log_entry *first_entry = malloc(sizeof(struct wfs_log_entry) + sizeof(first_dir));
    //set the name of the first directory to /
    strncpy(first_dir.name, "/", strlen(first_dir.name));
    first_dir.inode_number = 0;
    first_entry->inode.inode_number = 0;
    first_entry->inode.deleted = 0;
    first_entry->inode.mode = S_IFDIR;
    first_entry->inode.uid = getuid();
    first_entry->inode.gid = getgid();
    first_entry->inode.flags = 0;
    first_entry->inode.size = 0;
    //all 3 time fields are the current time
    first_entry->inode.mtime = creation_time;
    first_entry->inode.ctime = creation_time;
    first_entry->inode.links = 1;
    memmove(first_entry->data, &first_dir, sizeof(first_dir));
    int superblock_size = sizeof(struct wfs_sb);
    memmove(&file_buf[superblock_size], &first_entry, sizeof(first_entry) + sizeof(first_dir));
}

//li

///PTR START - SUPERBLOCK - FIRST LOG ENTRY
//file buf + sizeof superblock

//log entry
//inode (fixed size) -- this is what sizeof(wfs_log_enrty) returns
//data -- figure out size either an array of dir entries or file data
//size of entirre log entry = sizeof(wfs_log_entry) + sizeof(whatever the data is)


int main(int argc, char *argv[])
{
    if(argc != 2)
    {
	printf("Usage: mkfs.wfs <disk_path>\n");
	return 1;
    }
    int disk_fd = open(argv[1], O_RDWR);
    if(disk_fd == -1)
    {
	printf("Error opening disk file %s! (errno=%d)\n", argv[1], errno);
	return 1;
    }
    int disk_size = lseek(disk_fd, 0, SEEK_END);
    lseek(disk_fd, 0, SEEK_SET);
    printf("disk size=%d\n", disk_size);
    

    file_buf = (char*)mmap(NULL, disk_size, PROT_WRITE, MAP_SHARED, disk_fd, 0);


    printf("mmap successful (addr=0x%p)\n", file_buf);

    write_superblock();
    printf("sb write sucessful\n");
    write_log_entry();
    printf("log entry write successful\n");

    return 0;
}
