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
struct wfs_log_entry *head_entry;

int disk_size;

int largest_unused_inode_num = 1;

void create_inode(struct wfs_inode *inode, int mode, int num, int size)
{
    unsigned int creation_time = time(NULL);
    struct fuse_context *context  = fuse_get_context();
    inode->inode_number = num;
    inode->deleted = 0;
    inode->mode = mode | 0755;
    inode->uid = context->uid;
    inode->gid = context->gid;
    inode->flags = 0;
    inode->size = size;
    //all 3 time fields are the current time
    inode->atime = creation_time;
    inode->mtime = creation_time;
    inode->ctime = creation_time;
    inode->links = 1;
}

void create_dir_inode(struct wfs_inode *inode)
{
    create_inode(inode, S_IFDIR, num, 0);
}

//takes in an existing log entry representing a directory
//and sets head to be a new log entry with the same data
//except with the updated dirent at the end
void create_new_dir_entry(struct wfs_log_entry *parent, struct wfs_dentry *entry)
{
    if(!S_ISDIR(parent->inode.mode))
    {
	printf("(create_new_dir_entry) ERROR: called on a file\n");
	return;
    }
    //copy the provided entry into the head
    memmove(head_entry, parent, sizeof(struct wfs_log_entry) + parent->inode.size);
    //copy the provided dentry into the end of the head's array
    memmove(head_entry + sizeof(struct wfs_log_entry) + head_entry->inode.size,
	    entry, sizeof(struct wfs_dentry));

    //update size
    head_entry->inode.size += sizeof(struct wfs_dentry);
    //update head ptr to point to end
    head_entry = (struct wfs_log_entry *)((char*)head_entry + sizeof(struct wfs_log_entry) +
		  head_entry->inode.size);
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
    struct wfs_log_entry *res = NULL;
    unsigned int most_recent_time = 0;
    while((char*)cur_entry < disk_end)
    {
	
	//printf("cur_entry=[inode=%d,mtime=%d]\n", cur_entry->inode.inode_number, cur_entry->inode.mtime);
	if(cur_entry->inode.inode_number == inode && cur_entry->inode.mtime >= most_recent_time)
	{
	    
	    printf("cur_entry=0x%p\n", (void*)cur_entry);
	    res = cur_entry;
	    most_recent_time = cur_entry->inode.mtime;
	    if(cur_entry->inode.inode_number > largest_unused_inode_num)
	    {
		largest_unused_inode_num = cur_entry->inode.inode_numbe + 1r;
	    }
	}
	cur_entry = (struct wfs_log_entry *)(((char*)cur_entry)+cur_entry->inode.size+sizeof(struct wfs_inode));
    
    }
    return res;
}

struct wfs_log_entry *scan_dir_for_name(const struct wfs_log_entry *dir, const char *name)
{
    if(dir == NULL || !S_ISDIR(dir->inode.mode))
    {
	printf("scan_dir_for_name() called on something that's not a directory!\n");
	return NULL;
    }

    if(name == NULL)
	return NULL;
    
    struct wfs_dentry *entries = (struct wfs_dentry *)dir->data;
    for(int i = 0; i < dir->inode.size / sizeof(struct wfs_dentry); i++)
    {
	
	struct wfs_dentry cur_entry = entries[i];
	printf("(scan_dir_for_name) checking entry %s against name %s!\n", cur_entry.name, name);
	if((strlen(cur_entry.name) == strlen(name)) && strncmp(cur_entry.name, name, strlen(cur_entry.name)) == 0)
	{
	    printf("(scan_dir_for_name) match found between entry name %s and requested name %s!\n", cur_entry.name, name);
	    return get_inode(cur_entry.inode_number);
	}
    }

    return NULL;
}

struct wfs_log_entry *get_current_entry(const char *path)
{
    printf("get_current_entry called with path '%s'\n", path);
    int path_length;
    char **parsed = path_parser(path, &path_length);
    //start at root dir
    struct wfs_log_entry *cur_dir = get_inode(0);
printf("path=%s, strlen(path)=%ld\n", path, strlen(path));
    
    if(cur_dir == NULL)
    {
	printf("(get_current_entry) FATAL: couldnt find root dir!\n");
        
    }
    else if(strncmp(path, "/", strlen(path)) == 0)
    {
	printf("(get_current_entry) returning root entry\n");
	
    }
    else if(path_length == 1)
    {
	
	printf("(get_current_entry) root inode size %d\n", cur_dir->inode.size);
	printf("parsed[0] = %s, strlen=%ld\n", parsed[0], strlen(parsed[0]));
	if(strlen(parsed[0]) != 0)
	   cur_dir = scan_dir_for_name(cur_dir, parsed[0]);  
    }
    //otherwise, we need to walk directories until we reach the last entry
    else
    {
	for(int i = 0; i < path_length; i++)
	{
	    if(!S_ISDIR(cur_dir->inode.mode) || cur_dir->inode.deleted)
		continue;
	    printf("(get_current_entry) calling scan_dir_for_name with path fragment %s!\n", parsed[i]);
	    struct wfs_log_entry *next_dir = scan_dir_for_name(cur_dir, parsed[i]);
	    if(next_dir == NULL)
	    {
		printf("(get_current_entry) no result for fragment %s!", parsed[i]);
		return NULL;
	    }
	    printf("(get_current_entry) found fragment %s!\n", parsed[i]);
	    cur_dir = next_dir;
	}
    }
    if(cur_dir != NULL)
	printf("(get_current_entry) final result: [isdir=%d,inode=%d]\n", S_ISDIR(cur_dir->inode.mode), cur_dir->inode.inode_number);
    else
	printf("(get_current_entry) final result: not found\n");
    free_path(parsed, path_length);
    return cur_dir;
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
	printf("(getattr) no entry found for path %s\n", path);
	
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
    
    printf("wfs_mknod called!\n");
    int path_length;
    char** parsed_path = path_parser(path, &path_length);

    char *filename = parsed_path[path_length-1];
    
    parsed_path[--path_length] = NULL; //remove the last path segment
    struct wfs_log_entry *entry = get_current_entry(path);
    if(entry == NULL)
    {
	printf("(mknod) no parent entry found for path %s\n", path);
	
	return -ENOENT;
    }

    struct wfs_dentry new_entry;
    strcpy(new_entry.name, filename);
    new_entry.inode_number = largest_unused_inode_num;
    
    add_new_dir_entry(entry, new_entry);
    head_entry = (struct wfs_log_entry *)((char*)head_entry + sizeof(struct wfs_inode) + head_entry->inode.size);
    
    
    msync(disk_start, disk_size, MS_SYNC);
	
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
    {
	printf("(wfs_read) no entry for path %s!\n", path);
	return -ENOENT;
    }


    if(offset < entry->inode.size)
    {
	
	printf("(wfs_read) moving data... (inode %d)\n", entry->inode.inode_number);
	memmove(buf, entry->data + offset, entry->inode.size);
	return entry->inode.size;
    }
    else
    {
	printf("(wfs_read) offset is beyond inode size!\n");
	return -ENOENT;
    }
    
 
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{
    printf("wfs_write called\n");
    struct wfs_log_entry *entry = get_current_entry(path);
    if(entry == NULL)
    {
	printf("(wfs_write) no entry for path %s!\n", path);
	return -ENOENT;
    }

    entry->inode.size += size;
    
    memmove(entry->data + offset, buf, size);

    return size; 
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *info )
{

    printf("wfs_readdir called!\n");
    
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    info->fh = 0;
    
    
    struct wfs_log_entry *entry = get_current_entry(path);
    if(entry == NULL)
    {
	
	printf("(readdir) no entry found for path %s\n", path);
	return -ENOENT;
    }
    if(!S_ISDIR(entry->inode.mode) || entry->inode.deleted)
    {
	
	printf("(readdir) no entry found for path (is file) %s\n", path);
	    return -ENOENT;
    }

    int found = 0;
    for(int i = 0; i < entry->inode.size; i += sizeof(struct wfs_dentry))
    {
	struct wfs_dentry *cur_entry = (struct wfs_dentry *)(entry->data + i);

	struct stat st;
	wfs_getattr(path, &st);
	if(st.st_ino == 0)
	{
	    filler(buf, "/", &st, 0);
	    
	    filler(buf, ".", &st, 0);
	    filler(buf, "..", &st, 0);
	    found = 1;
	    continue;
	}
	
	printf("calling filler on %s\n", cur_entry->name);
	if(filler(buf, cur_entry->name, &st, 0) != 0)
	{
	    printf("Error: filler buffer full!\n");
	    return -ENOMEM;
	}
	found = 1;
	
	    
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
    head_entry = (struct wfs_log_entry *)(disk_start + superblock.head);
    printf("first_entry addr=0x%p\n", (void*)first_entry);
        printf("head_entry addr=0x%p\n", (void*)head_entry);
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
