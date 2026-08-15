
struct iconIndexDir {
    char *name;
    char *path;
    int Size;
    int Scale;
    char *Context;
    int Type;
    int MaxSize;
    int MinSize;
    int Threshold;
};

struct iconIndexFile {
    char *Name;
    char *Comment;
    char *Inherits;
    char *Directories;
    char *ScaledDirectories;
    int Hidden;
    char *Example;

    struct iconIndexDir **subDirs;
    int subDirCount;
    int subDirListSize;
};

int parseIconIndex(char *filename, struct iconIndexFile *iif);
