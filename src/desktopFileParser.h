struct desktopGroup{
    char* groupName;
    //bools
    int NoDisplay;
    int Hidden;
    int DBusActivatable;
    int Terminal;
    int StartupNotify;
    int PrefersNonDefaultGPU;
    int SingleMainWindow;
    //3 states
    int Type;
    //strings
    char* Version;
    char* Icon;
    char* TryExec;
    char* Exec;
    char* Path;
    char* StartupWMClass;
    char* URL;

    //locale string (idfk how to do this well rn)
    char* Name;
    char* GenericName;
    char* Comment;

    //string arrays, also unsure how to handle this rn, for now just not split up
    char* OnlyShowIn;
    char* NotShowIn;
    char* Actions;
    char* MimeType;
    char* Categories;
    char* Implements;

    //arrays of locale strings, even worse xd
    char* Keywords;
};

struct desktopFile {
    char *filename;
    struct desktopGroup **groups;
    int count;
};

struct desktopFileList {
    struct desktopFile **files;
    int count;
    int size;
};

struct desktopFileList *parseDFDirs(char *paths[], int pathCnt);
