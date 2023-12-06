#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "wfs.h"

#define MAX_PATH 20

int* start; //Address for start of disk.
int* end; //Address where last log ended.
int* current; //Address of current place.

//Splits up the path using / as the delimiter and returns it in an array. 
char** path_parser(const char *path) {
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

void move_dir(wfs_log_entry entry){
    current += //Size of inode and data.
}

void move_file(wfs_log_entry entry){
    current += //Size of inode and data.
}

static int wfs_getattr(const char *path, struct stat *stbuf)
{
    char** parsed_path = path_parser(path);
    // Implementation of getattr function to retrieve file attributes
    // Fill stbuf structure with the attributes of the file/directory indicated by path
    // ...
    free_path(parsed_path, MAX_PATH);
    return 0; // Return 0 on success
}

static int wfs_mknod(const char *path, struct stat *stbuf)
{

    return 0; // Return 0 on success
}

static int wfs_mkdir(const char *path, struct stat *stbuf)
{

    return 0; // Return 0 on success
}

static int wfs_read(const char *path, struct stat *stbuf)
{

    return 0; // Return 0 on success
}

static int wfs_write(const char *path, struct stat *stbuf)
{

    return 0; // Return 0 on success
}

static int wfs_readdir(const char *path, struct stat *stbuf)
{

    return 0; // Return 0 on success
}

static int wfs_unlink(const char *path, struct stat *stbuf)
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
    //Remove disk path from argv for fuse_main
    argv[argc - 2] = argv[argc-1];
    argv[argc - 1] = NULL;
    argc--;

    // Initialize FUSE with specified operations
    // Filter argc and argv here and then pass it to fuse_main
    return fuse_main(argc, argv, &ops, NULL);
}
