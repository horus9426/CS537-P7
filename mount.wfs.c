#define FUSE_USE_VERSION 30
#include <errno.h>
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wfs.h"

#define MAX_PATH 20

char *disk_start; //Address for start of disk.
char *disk_end; //Address where last log ended.
char *disk_current; //Address of current place.

struct wfs_log_entry *first_entry;

int disk_size;

int cur_inode_num = 1;

void fill_dir_inode(struct wfs_inode *inode, int refcnt)
{   
    unsigned int creation_time = time(NULL);
    inode->inode_number = cur_inode_num++;
    inode->deleted = 0;
    inode->mode = S_IFDIR;
    inode->uid = getuid();
    inode->gid = getgid();
    inode->flags = 0;
    inode->size = sizeof(struct wfs_log_entry) + sizeof(struct wfs_dentry);
    //all 3 time fields are the current time
    inode->atime = creation_time;
    inode->mtime = creation_time;
    inode->ctime = creation_time;
    inode->links = refcnt;
}

//Splits up the path using / as the delimiter and returns it in an array. 
char** path_parser(const char *path, int *path_length) {
    int max_path = MAX_PATH; //For some reason, I can't use MAX_PATH without it not working.
    char** new_path = (char**)malloc(max_path * sizeof(char*)); //Dynamically allocate variable for use outside of function.

    // Initialize each string in new_path
    for (int i = 0; i < max_path; i++) {
        new_path[i] = (char*)malloc(MAX_PATH * sizeof(char));
    }

    char *pathCopy = strdup(path);
    char *token = strtok(pathCopy, "/");
    int tokenCount = 0;

    // Save tokens into the array
    while (token != NULL && tokenCount < max_path) {
        strcpy(new_path[tokenCount], token);
        tokenCount++;

        // Get the next token
        token = strtok(NULL, "/");
    }

    free(pathCopy);

    *path_length = tokenCount;
    // Return the array of strings
    return new_path;
}

//Frees the memory created when extracting a path.
void free_path(char** path, int max_path) {
    for (int i = 0; i < max_path; i++) {
        free(path[i]);
    }
    free(path);
}

/* void move_dir(wfs_log_entry entry){ */
/*     current += //Size of inode and data. */
/* } */

/* void move_file(wfs_log_entry entry){ */
/*     current += //Size of inode and data. */
/* } */

//gets a log entry from an inode number
struct wfs_log_entry *get_inode(unsigned long inode)
{
    struct wfs_log_entry *cur_entry = first_entry;
    printf("b (0x%p)\n", (void*)cur_entry);
    while((char*)cur_entry < disk_end)
    {
	printf("cur_entry=0x%p\n", (void*)cur_entry);
	if(cur_entry->inode.inode_number == inode && cur_entry->inode.links)
	    return cur_entry;
	cur_entry = (struct wfs_log_entry *)(((char*)cur_entry)+cur_entry->inode.size+sizeof(struct wfs_inode));
    
    }
    return NULL;
}

struct wfs_log_entry *get_current_entry(const char *path)
{
    printf("get_current_entry called with path %s\n", path);
    int path_length;
    char **parsed = path_parser(path, &path_length);
    if(path_length == 0)
    {
	printf("returning root entry\n");
	return get_inode(0);
    }

    else if(path_length == 1)
    {
	struct wfs_log_entry *root = get_inode(0);
	if(root == NULL)
	{
	    printf("couldn't find root inode br!\n");
	    return NULL;
	}
	printf("root inode size %d\n", root->inode.size);
	for(int j = 0; j < root->inode.size; j += sizeof(struct wfs_dentry))
	{
	    struct wfs_dentry *cur_entry = (struct wfs_dentry *)(root->data + j);
	    printf("checking root dir for name %s (entry name is %s)\n", path, cur_entry->name);

	    if(strncmp(cur_entry->name, &path[1], strlen(cur_entry->name)) == 0)
	    {
		printf("found a match between path %s and dirent %s!\n", &path[1], cur_entry->name);
		return get_inode(cur_entry->inode_number);
		
	    }
	    
	}
	return NULL;
    }

    //otherwise, we need to walk directories until we reach the last entry
    struct wfs_log_entry *parent_entry = get_inode(0);
    for(int i = 0; i < path_length; i++)
    {
	if(parent_entry->inode.mode != S_IFDIR || parent_entry->inode.deleted)
	    continue;
	for(int j = 0; j < parent_entry->inode.size; j += sizeof(struct wfs_dentry))
	{
	    struct wfs_dentry *cur_entry = (struct wfs_dentry *)(parent_entry->data + j);
	    printf("checking dir name %s for entry %s\n" , cur_entry->name, parsed[i]);
	    if(strncmp(cur_entry->name, parsed[i], strlen(cur_entry->name)) == 0)
	    {
		printf("found a match between path %s and dirent %s!\n", parsed[i], cur_entry->name);
		return get_inode(cur_entry->inode_number);
		
	    }
	    
	    
	}
    }
    
    free_path(parsed, path_length);
    return NULL;
}



static int wfs_getattr(const char *path, struct stat *stbuf)
{
    printf("wfs_getattr called\n");
    int path_length;
    char** parsed_path = path_parser(path, &path_length);
    // Implementation of getattr function to retrieve file attributes
    // Fill stbuf structure with the attributes of the file/directory indicated by path
    struct wfs_log_entry *entry = get_current_entry(path);
    if(entry == NULL)
    {
	printf("no entry found for path %s\n", path);
	
	return -ENOENT;
    }
    struct wfs_inode *inode = &entry->inode;
    
    stbuf->st_ino = inode->inode_number;
    stbuf->st_mode = inode->mode;
    stbuf->st_uid = inode->uid;
    stbuf->st_gid = inode->gid;
    stbuf->st_size = inode->size;
    stbuf->st_atime = inode->atime;
    stbuf->st_mtime = inode->mtime;
    stbuf->st_ctime = inode->ctime;
    stbuf->st_nlink = inode->links;
    
    free_path(parsed_path, path_length);
    return 0; // Return 0 on success
}

static int wfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    struct wfs_log_entry *cur_entry = first_entry;
    while(cur_entry->inode.size != 0)
    {
	
        
	
	printf("skipping inode of size %d!\n", cur_entry->inode.size);
	cur_entry = (struct wfs_log_entry *)(((char*)cur_entry)+cur_entry->inode.size);
    
    }
fill_dir_inode(&cur_entry->inode, 1);
	
    return 0; // Return 0 on success
}

static int wfs_mkdir(const char *path, mode_t mode)
{

    return 0; // Return 0 on success
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{
    printf("wfs_read called\n");
    struct wfs_log_entry *entry = get_current_entry(path);
    if(entry == NULL)
	return -ENOENT;

    
    return 0; // Return 0 on success
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{

    return 0; // Return 0 on success
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *info )
{

    printf("wfs_readdir called!\n");
    
    	
    
    struct wfs_log_entry *entry = get_current_entry(path);
    if(entry == NULL)
    {
	
	printf("no entry found for path %s\n", path);
	return -ENOENT;
    }
    if(entry->inode.mode != S_IFDIR || entry->inode.deleted)
    {
	
	printf("no entry found for path %s\n", path);
	    return -ENOENT;
    }

    int found = 0;
    for(int i = 0; i < entry->inode.size; i += sizeof(struct wfs_dentry))
    {
	struct wfs_dentry *cur_entry = (struct wfs_dentry *)(entry->data + i);
	struct stat st;
	wfs_getattr(path, &st);
	if(st.st_mode == S_IFDIR)
	{
	    printf("calling filler on %s\n", cur_entry->name);
	    if(filler(buf, cur_entry->name, &st, 0) != 0)
	    {
		printf("Error: filler buffer full!\n");
		return -ENOMEM;
	    }
	    found = 1;
	}
	    
    }

    if(!found)
    {
	printf("no entry found for path %s\n", path);
	return -ENOENT;
    }

    
    return 0; 
}

static int wfs_unlink(const char *path)
{

    return 0; // Return 0 on success
}

static struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .mknod = wfs_mknod,
    .mkdir = wfs_mkdir,
    .read = wfs_read,
    .write = wfs_write,
    .readdir = wfs_readdir,
    .unlink = wfs_unlink,
};

int main(int argc, char *argv[])
{
    if((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-2][0] == '-'))
    {
	printf("Usage: mount.wfs [FUSE args] [disk file] [mount point]\n");
	return -1;
    }

    int disk_file_fd = open(argv[argc-2], O_RDWR);
    if(disk_file_fd == -1)
    {
	printf("Error opening disk file %s! (errno=%d)\n", argv[argc-2], errno);
	return -1;
    }

    disk_size = lseek(disk_file_fd, 0, SEEK_END);
    lseek(disk_file_fd, 0, SEEK_SET);

    printf("disk image size: %d\n", disk_size);
    disk_start = (char*)mmap(NULL, disk_size, PROT_READ|PROT_WRITE, MAP_SHARED, disk_file_fd, 0);
    disk_end = disk_start + disk_size;

    printf("disk file mapped to vaddr 0x%p\n", disk_start);


    //validate disk file by checking for magic number
    struct wfs_sb superblock = *((struct wfs_sb *)disk_start);
    if(superblock.magic != WFS_MAGIC)
    {
	printf("disk file %s is not a valid WFS disk image!\n", argv[argc-2]);
	return 1;
    }
    first_entry = (struct wfs_log_entry *)(disk_start + sizeof(struct wfs_sb));

    printf("first_entry addr=0x%p\n", (void*)first_entry);
    /* int path_len; */
    /* char **test = path_parser("/", &path_len); */
    /* printf("path_len of / is %d\n", path_len); */
    /* for(int i = 0; i < path_len; i++) */
    /* 	printf("path[%d] = %s\n", i, test[i]); */
    /* free_path(test, path_len); */
    /* test = path_parser("/bruh", &path_len); */
    /* printf("path_len of /bruh is %d\n", path_len); */
    /* for(int i = 0; i < path_len; i++) */
    /* 	printf("path[%d] = %s\n", i, test[i]); */
    /* free_path(test, path_len); */
    
    //Remove disk path from argv for fuse_main
    argv[argc - 2] = argv[argc-1];
    argv[argc - 1] = NULL;
    argc--;

    // Initialize FUSE with specified operations
    // Filter argc and argv here and then pass it to fuse_main
    int ret = fuse_main(argc, argv, &ops, NULL);

    //unmap and close the disk file
    munmap(disk_start, disk_size);
    close(disk_file_fd);
    return ret;
}
