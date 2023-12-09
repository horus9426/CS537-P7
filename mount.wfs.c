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

char *disk_start;   // Address for start of disk.
char *disk_end;     // Address where last log ended.
char *disk_current; // Address of current place.

struct wfs_log_entry *first_entry;
struct wfs_log_entry *head_entry;

int disk_size;

int largest_unused_inode_num = 1;

// Splits up the path using / as the delimiter and returns it in an array.
char **path_parser(const char *path, int *path_length)
{
    int max_path = MAX_PATH;                                      // For some reason, I can't use MAX_PATH without it not working.
    char **new_path = (char **)malloc(max_path * sizeof(char *)); // Dynamically allocate variable for use outside of function.

    // Initialize each string in new_path
    for (int i = 0; i < max_path; i++)
    {
        new_path[i] = (char *)malloc(MAX_PATH * sizeof(char));
    }

    char *pathCopy = strdup(path);
    char *token = strtok(pathCopy, "/");
    int tokenCount = 0;

    // Save tokens into the array
    while (token != NULL && tokenCount < max_path)
    {
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

// Frees the memory created when extracting a path.
void free_path(char **path, int max_path)
{
    for (int i = 0; i < max_path; i++)
    {
        free(path[i]);
    }
    free(path);
}

//takes in a char double-array (returned by parser) and reconstructs
//a path from it's contents
char *reconstruct_path(char **parsed, int path_length)
{
    int len = 0;
    for(int i = 0; i < path_length; i++)
	len += strlen(parsed[i]);

    char *res = malloc(len + 1 + path_length);

    *res = '/';
    int offset = 1;
    for(int i = 0; i < path_length; i++)
    {
	strcpy(res + offset, parsed[i]);
	offset += strlen(parsed[i]);
	res[offset++] = '/';
    }

    res[offset] = 0;

    printf("(reconstruct_path) reconstructed path = %s\n", res);
    return res;
    
}

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


//fils inode with the requisite data to represent an empty directory
void create_dir_inode(struct wfs_inode *inode, int num)
{
    create_inode(inode, S_IFDIR | 0755, num, 0);
}

//fils inode with the requisite data to represent an empty file
void create_file_inode(struct wfs_inode *inode, int num)
{
    create_inode(inode, S_IFREG | 0755, num, 0);
}

//takes in an existing log entry representing a directory
//and sets head to be a new log entry with the same data
//except with the updated dirent at the end
void add_entry_to_dir(struct wfs_log_entry *parent, struct wfs_dentry *entry)
{
    if(!S_ISDIR(parent->inode.mode))
    {
	printf("(add_entry_to_dir) ERROR: called on a file\n");
	return;
    }
    printf("(add_entry_to_dir) called, adding entry %s to inode %d!\n", entry->name, parent->inode.inode_number);
    printf("(add_entry_to_dir) head ptr before: 0x%p, head size %d\n", (void*)head_entry, head_entry->inode.size);
    size_t head_size = sizeof(struct wfs_inode) + parent->inode.size;
    
    //copy the provided entry into the head
    memmove(head_entry, parent, head_size);
    //copy the provided dentry into the end of the head's array
    memmove((char*)head_entry + sizeof(struct wfs_log_entry) + parent->inode.size,
	    entry, sizeof(struct wfs_dentry));

    //update size
    head_entry->inode.size += sizeof(struct wfs_dentry);


    struct wfs_log_entry *prev_head = head_entry;
    //update head ptr to point to end
    head_entry = (struct wfs_log_entry *)((char*)head_entry + sizeof(struct wfs_log_entry) + head_entry->inode.size);

    struct wfs_sb sb = *(struct wfs_sb *)disk_start;
    sb.head = ((char*)head_entry - disk_start);
    printf("(add_entry_to_dir) head ptr after: 0x%p, head size %d\n", (void*)prev_head, prev_head->inode.size);
    printf("new sb head: %d\n", sb.head);
}

//takes in an existing log entry representing a directory
//and sets head to be a new log entry with the same data
//except with the path provided being removed from the dirent array
void remove_entry_from_dir(struct wfs_log_entry *parent, const char *name)
{
    if(!S_ISDIR(parent->inode.mode))
    {
	printf("(remove_entry_from_dir) ERROR: called on a file\n");
	return;
    }
    printf("(remove_entry_from_dir) called, removing entry %s from inode %d!\n", name, parent->inode.inode_number);
    printf("(remove_entry_from_dir) head ptr before: 0x%p, head size %d\n", (void*)head_entry, head_entry->inode.size);

    size_t parent_size = sizeof(struct wfs_log_entry) + parent->inode.size;
    struct wfs_log_entry *new_entry = malloc(parent_size);

    memmove(new_entry, parent, parent_size);
    struct wfs_dentry *dir_entries = (struct wfs_dentry *)new_entry->data;
    for(int i = 0; i < new_entry->inode.size / sizeof(struct wfs_dentry); i++)
    {
	struct wfs_dentry *cur_entry = &dir_entries[i];
	if(strlen(cur_entry->name) == strlen(name) && strncmp(cur_entry->name, name, strlen(cur_entry->name)) == 0)
	{
	    printf("(remove_entry_from_dir) found dirent %s matching requested \
		   file to be removed %s!\n", cur_entry->name, name);

	    //move the rest of the entries over by 1
	    memmove(cur_entry, (char*)cur_entry + sizeof(struct wfs_dentry),
		    new_entry->inode.size - (i * sizeof(struct wfs_dentry)));
	    break;
	}
    }
    new_entry->inode.size -= sizeof(struct wfs_dentry);
    
    memmove(head_entry, new_entry, sizeof(struct wfs_log_entry)
	    + new_entry->inode.size);

    
    head_entry = (struct wfs_log_entry *)((char*)head_entry + sizeof(struct wfs_log_entry) + head_entry->inode.size);
    printf("(remove_entry_from_dir) head ptr after: 0x%p, head size %d\n", (void*)head_entry, head_entry->inode.size);

    free(new_entry);
}

//adds a file log enrty
void add_file_log_entry(int inode_num)
{
    printf("(add_file_log_entry) adding a new log entry for a new inode %d!\n", inode_num);
    create_file_inode(&head_entry->inode, inode_num);
    head_entry = (struct wfs_log_entry *)((char*)head_entry + sizeof(struct wfs_log_entry)+head_entry->inode.size);
}

// gets a log entry from an inode number
struct wfs_log_entry *get_inode(unsigned long inode)
{
    struct wfs_log_entry *cur_entry = first_entry;
    struct wfs_log_entry *res = NULL;
    unsigned int most_recent_time = 0;
    while ((char *)cur_entry < disk_end)
    {
	
	//printf("cur_entry=[inode=%d,mtime=%d]\n", cur_entry->inode.inode_number, cur_entry->inode.mtime);
	if(cur_entry->inode.inode_number == inode && !cur_entry->inode.deleted && cur_entry->inode.mtime >= most_recent_time)
	{
	    
	    printf("cur_entry=0x%p\n", (void*)cur_entry);
	    res = cur_entry;
	    most_recent_time = cur_entry->inode.mtime;
	    if(cur_entry->inode.inode_number > largest_unused_inode_num)
	    {
		largest_unused_inode_num = cur_entry->inode.inode_number + 1;
	    }
	}
	cur_entry = (struct wfs_log_entry *)(((char*)cur_entry)+cur_entry->inode.size+sizeof(struct wfs_inode));
    
    }
    return res;
}

struct wfs_log_entry *scan_dir_for_name(const struct wfs_log_entry *dir, const char *name)
{
    if (dir == NULL || !S_ISDIR(dir->inode.mode))
    {
        printf("scan_dir_for_name() called on something that's not a directory!\n");
        return NULL;
    }

    if(name == NULL)
	return NULL;
    
    struct wfs_dentry *entries = (struct wfs_dentry *)dir->data;
    for (int i = 0; i < dir->inode.size / sizeof(struct wfs_dentry); i++)
    {

        struct wfs_dentry cur_entry = entries[i];
        printf("(scan_dir_for_name) checking entry %s against name %s!\n", cur_entry.name, name);
        if ((strlen(cur_entry.name) == strlen(name)) && strncmp(cur_entry.name, name, strlen(cur_entry.name)) == 0)
        {
            printf("(scan_dir_for_name) match found between entry name %s and requested name %s! (inode %ld)\n", cur_entry.name, name, cur_entry.inode_number);
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
    // start at root dir
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
    // otherwise, we need to walk directories until we reach the last entry
    else
    {
        for (int i = 0; i < path_length; i++)
        {
            if (!S_ISDIR(cur_dir->inode.mode) || cur_dir->inode.deleted)
                continue;
            printf("(get_current_entry) calling scan_dir_for_name with path fragment %s!\n", parsed[i]);
            struct wfs_log_entry *next_dir = scan_dir_for_name(cur_dir, parsed[i]);
            if (next_dir == NULL)
            {
                printf("(get_current_entry) no result for fragment %s!", parsed[i]);
                return NULL;
            }
            printf("(get_current_entry) found fragment %s!\n", parsed[i]);
            cur_dir = next_dir;
        }
    }
    if (cur_dir != NULL)
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
    char **parsed_path = path_parser(path, &path_length);
    // Implementation of getattr function to retrieve file attributes
    // Fill stbuf structure with the attributes of the file/directory indicated by path
    struct wfs_log_entry *entry = get_current_entry(path);
    if (entry == NULL)
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
    char *parent_path = reconstruct_path(parsed_path, path_length);
    struct wfs_log_entry *entry = get_current_entry(parent_path);
    if(entry == NULL)
    {
	printf("(mknod) no parent entry found for path %s\n", path);
	
	return -ENOENT;
    }

    struct wfs_dentry new_entry;
    strcpy(new_entry.name, filename);
    printf("new entry name: %s\n", new_entry.name);
    new_entry.inode_number = largest_unused_inode_num;
    
    add_entry_to_dir(entry, &new_entry);
    add_file_log_entry(new_entry.inode_number);
    
    
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
    if (entry == NULL)
    {
        printf("(wfs_read) no entry for path %s!\n", path);
        return -ENOENT;
    }

    if (offset < entry->inode.size)
    {

        printf("(wfs_read) moving data... (inode %d)\n", entry->inode.inode_number);
        memmove(buf, entry->data + offset, entry->inode.size);
        return entry->inode.size;
    }
    else
    {
        printf("(wfs_read) offset is beyond inode size!\n");
        return 0;
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

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *info)
{

    printf("wfs_readdir called!\n");

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    info->fh = 0;

    struct wfs_log_entry *entry = get_current_entry(path);
    if (entry == NULL)
    {

        printf("(readdir) no entry found for path %s\n", path);
        return -ENOENT;
    }
    if (!S_ISDIR(entry->inode.mode) || entry->inode.deleted)
    {

        printf("(readdir) no entry found for path (is file) %s\n", path);
        return -ENOENT;
    }

   
    for (int i = 0; i < entry->inode.size; i += sizeof(struct wfs_dentry))
    {
        struct wfs_dentry *cur_entry = (struct wfs_dentry *)(entry->data + i);

	/* struct stat st; */
	
	/* wfs_getattr(path, &st); */
	/* if(st.st_ino == 0) */
	/* { */
	/*     filler(buf, "/", &st, 0); */
	    
	/*     filler(buf, ".", &st, 0); */
	/*     filler(buf, "..", &st, 0); */
	/*     found = 1; */
	/*     continue; */
	/* } */
	
	printf("calling filler on %s\n", cur_entry->name);
	if(filler(buf, cur_entry->name, NULL, 0) != 0)
	{
	    printf("Error: filler buffer full!\n");
	    return -ENOMEM;
	}
	
	
	    
    }

    

    return 0;
}

// Function to copy a directory entry excluding the specified name
void copy_dentry(struct wfs_dentry* src_dentry, struct wfs_log_entry* new_parent, const char* exclude_name) {
    // Check if the entry should be excluded
    if (strcmp(src_dentry->name, exclude_name) == 0) {
        return;  // Skip the entry
    }

    // Create a new directory entry in the destination
    struct wfs_dentry* dest_dentry = (struct wfs_dentry*)((char*)new_parent->data + new_parent->inode.size);
    strcpy(dest_dentry->name, src_dentry->name);
    dest_dentry->inode_number = src_dentry->inode_number;

    // Update the destination directory's size
    new_parent->inode.size += sizeof(struct wfs_dentry);
}


static int wfs_unlink(const char *path)
{
    printf("(wfs_unlink) called with path %s\n", path);
    char *parent_path = strdup(path); // Variable that changes parent directory
    char *path_copy = strdup(path); //Used to extract the last component of the path.

    // Update targeted Inode.
    struct wfs_log_entry *entry = get_current_entry(path);
    if (entry == NULL)
    {
        printf("(wfs_unlink) no entry for path %s!\n", path);
        return -ENOENT;
    }
    if (!S_ISREG(entry->inode.mode))
    {
        printf("(wfs_unlink) path is not a file %s!\n", path);
        return -EISDIR;
    }
    unsigned int deletion_time = time(NULL);
    
    entry->inode.deleted = 1;
    entry->inode.atime = deletion_time;
    entry->inode.mtime = deletion_time;

    // Update Parent directory
    char *last_slash = strrchr(parent_path, '/');

    if (last_slash != parent_path)
    {
	printf("(wfs_unlink) unlinking in a non-root dir!\n");
        *last_slash = '\0';
    }
    else
    {
	printf("(wfs_unlink) unlinking in root directory!\n");
        // If no '/', it means the path is already the root directory
	
        free(parent_path);
	parent_path = malloc(2);
	parent_path[0] = '/';
	parent_path[1] = 0;
	
    }

    //Create new log entry for parent directory.
    struct wfs_log_entry *parent = get_current_entry(parent_path);
    struct wfs_log_entry *new_parent = (struct wfs_log_entry *)malloc(sizeof(struct wfs_log_entry));
    if (new_parent == NULL)
    {
        printf("Memory allocation error!\n");
        free(parent_path);
        free(path_copy);
        return -ENOMEM;
    }
    memset(new_parent, 0, sizeof(struct wfs_log_entry));
    char* last_component;

    char* get_last_component = strrchr(path_copy, '/');

    if (get_last_component != NULL) {
        last_component = get_last_component + 1;
    }

    unsigned int creation_time = time(NULL);

    //Inode
    memmove(&new_parent->inode, &parent->inode, sizeof(struct wfs_inode));
    new_parent->inode.atime = creation_time;
    new_parent->inode.mtime = creation_time;

    remove_entry_from_dir(new_parent, last_component);
    
    /* (*new_parent).inode.inode_number = (*parent).inode.inode_number; */
    /* (*new_parent).inode.deleted = (*parent).inode.deleted; */
    /* (*new_parent).inode.mode = (*parent).inode.mode; */
    /* (*new_parent).inode.uid = (*parent).inode.uid; */
    /* (*new_parent).inode.gid = (*parent).inode.gid; */
    /* (*new_parent).inode.flags = (*parent).inode.flags; */
    /* (*new_parent).inode.links = (*parent).inode.links; */
    /* (*new_parent).inode.ctime = creation_time; */
    /* (*new_parent).inode.atime = creation_time; */
    /* (*new_parent).inode.mtime = creation_time; */

    /* //Directory information */
    /* struct wfs_dentry* src_entries = (struct wfs_dentry*)parent->data; */
    /* for (int i = 0; i < parent->inode.size / sizeof(struct wfs_dentry); ++i) { //MIGHT NEED ALTERATIONS DEPENDING ON WHETHER SIZE OF INODE INCLUDES INODE ITSELF */
    /*     copy_dentry(&src_entries[i], new_parent, last_component); */
    /* } */
    /* memmove(head_entry, new_parent, new_parent->inode.size); */
    /* 	   head_entry = (struct wfs_log_entry *)((char*)head_entry + new_parent->inode.size + sizeof(struct wfs_inode)); */

    msync(disk_start, disk_size, MS_SYNC);
    free(parent_path);
    free(path_copy);

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
    if ((argc < 3) || (argv[argc - 2][0] == '-') || (argv[argc - 2][0] == '-'))
    {
        printf("Usage: mount.wfs [FUSE args] [disk file] [mount point]\n");
        return -1;
    }

    int disk_file_fd = open(argv[argc - 2], O_RDWR);
    if (disk_file_fd == -1)
    {
        printf("Error opening disk file %s! (errno=%d)\n", argv[argc - 2], errno);
        return -1;
    }

    disk_size = lseek(disk_file_fd, 0, SEEK_END);
    lseek(disk_file_fd, 0, SEEK_SET);

    printf("disk image size: %d\n", disk_size);
    disk_start = (char *)mmap(NULL, disk_size, PROT_READ | PROT_WRITE, MAP_SHARED, disk_file_fd, 0);
    disk_end = disk_start + disk_size;

    printf("disk file mapped to vaddr 0x%p\n", disk_start);

    // validate disk file by checking for magic number
    struct wfs_sb superblock = *((struct wfs_sb *)disk_start);
    if (superblock.magic != WFS_MAGIC)
    {
        printf("disk file %s is not a valid WFS disk image!\n", argv[argc - 2]);
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

    // Remove disk path from argv for fuse_main
    argv[argc - 2] = argv[argc - 1];
    argv[argc - 1] = NULL;
    argc--;

    // Initialize FUSE with specified operations
    // Filter argc and argv here and then pass it to fuse_main
    int ret = fuse_main(argc, argv, &ops, NULL);

    // unmap and close the disk file
    munmap(disk_start, disk_size);
    close(disk_file_fd);
    return ret;
}
