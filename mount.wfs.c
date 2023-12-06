#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>


char* path_parser (const char *path){
    int max_path = 20;
    char** new_path = (char**)malloc(max_path * sizeof(char*));
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

    // Print the tokens
    for (int i = 0; i < tokenCount; i++) {
        printf("%s\n", new_path[i]);
    }
    free(pathCopy);
    return new_path;
}

static int wfs_getattr(const char *path, struct stat *stbuf)
{
    char* parsed_path = path_parser(path);
    // Implementation of getattr function to retrieve file attributes
    // Fill stbuf structure with the attributes of the file/directory indicated by path
    // ...

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
