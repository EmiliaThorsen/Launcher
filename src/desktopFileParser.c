#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include "desktopFileParser.h"
#include "iniFileParsingUtils.h"

#define E(state,c,new) [state*128+(c)]=new,
int transTable[128*15] = {
    E(1,'A',-1) //Actions
    E(1,'C',2)  E(2,'a',-2) //Categories
                E(2,'o',-3) //Comment
    E(1,'D',-4) //DBusActivatable
    E(1,'E',-5) //Exec
    E(1,'G',-6) //GenericName
    E(1,'H',-7) //Hidden
    E(1,'I',3)  E(3,'c',-8) //Icon
                E(3,'m',-9) //Implements
    E(1,'K',-10) //Keywords
    E(1,'M',-11) //MimeType
    E(1,'N',4)  E(4,'a',-12) //Name
                E(4,'o',5)  E(5,'D',-13) //NoDisplay
                            E(5,'t',-14) //NotShowIn
    E(1,'O',-15) //OnlyShowIn
    E(1,'P',6)  E(6,'a',-16) //Path
                E(6,'r',-17) //PreferNonDefaultGPU
    E(1,'S',7)  E(7,'i',-18) //SingleMainWindow
                E(7,'t',8) E(8,'a',9) E(9,'r',10) E(10,'t',11) E(11,'u',12) E(12,'p',13) E(13,'N',-19) //startupNotify
                                                                                         E(13,'W',-20) //startupWMClass
    E(1,'T',14) E(14,'e',-21) //Terminal
                E(14,'r',-22) //TryExec
                E(14,'y',-23) //Type
    E(1,'U',-24) //URL
    E(1,'V',-25) //Version
};
#undef E

const static char *keysWOStart[25] = {"ctions", "tegories", "mment", "BusActivatable", "xec", "enericName", "idden", "on", "plements", "eywords", "imeType", "me", "isplay", "ShowIn", "nlyShowIn", "th", "efersNonDefaultGPU", "ngleMainWindow", "otify", "MClass", "rminal", "yExec", "pe", "RL", "ersion"};


void desktopGroupStateParser(FILE *file, int state, void *g) {
    struct desktopGroup *group = g;
    switch (state) {
        case 0:  group->Actions = stringParser(file); break;
        case 1:  group->Categories = stringParser(file); break;
        case 2:  group->Comment = stringParser(file); break;
        case 3:  group->DBusActivatable = boolParser(file); break;
        case 4:  group->Exec = stringParser(file); break;
        case 5:  group->GenericName = stringParser(file); break;
        case 6:  group->Hidden = boolParser(file); break;
        case 7:  group->Icon = stringParser(file); break;
        case 8:  group->Implements = stringParser(file); break;
        case 9:  group->Keywords = stringParser(file); break;
        case 10: group->MimeType = stringParser(file); break;
        case 11: group->Name = stringParser(file); break;
        case 12: group->NoDisplay = boolParser(file); break;
        case 13: group->NotShowIn = stringParser(file); break;
        case 14: group->OnlyShowIn = stringParser(file); break;
        case 15: group->Path = stringParser(file); break;
        case 16: group->PrefersNonDefaultGPU = boolParser(file); break;
        case 17: group->SingleMainWindow = boolParser(file); break;
        case 18: group->StartupNotify = boolParser(file); break;
        case 19: group->StartupWMClass = stringParser(file); break;
        case 20: group->Terminal = boolParser(file); break;
        case 21: group->TryExec = stringParser(file); break;
        case 22: group->Type = typeParser(file); break;
        case 23: group->URL = stringParser(file); break;
        case 24: group->Version = stringParser(file); break;
    }
}


int parseDesktopFile(char *filename, struct desktopFile *DTFile) {
    DTFile->groups = malloc(sizeof(struct desktopGroup *) * 4);
    int size = 4;
    DTFile->count = 0;
    DTFile->filename = filename;

    FILE *file = fopen(filename, "r");
    if(file == NULL) {
        free(DTFile->groups);
        DTFile->groups = NULL;
        return -1;
    }

    int c;
    while ((c = fgetc(file)) != '[' && c != EOF);
    if (c == EOF) { fclose(file); return -1; }
    ungetc('[', file);

    while(1) {
        struct desktopGroup *group = malloc(sizeof(struct desktopGroup));
        memset(group, 0, sizeof(*group));
        struct iniSectionDef isd = {
            .caseHandler = desktopGroupStateParser,
            .data = group,
            .endings = (char **)keysWOStart,
            .transTable = transTable
        };
        int result = parseIniSection(file, &isd);

        if(result == -1) {
            free(group);
            fclose(file);
            return -1;
        }

        group->groupName = isd.sectionName;

        if(DTFile->count == size) {
            DTFile->groups = realloc(DTFile->groups, sizeof(struct desktopGroup *) * size * 2);
            size *= 2;
        }
        DTFile->groups[DTFile->count] = group;
        DTFile->count++;
        if(result == 0) break;
    }
    fclose(file);
    return 0;
}

struct desktopFileList *initFileList() {
    struct desktopFileList *list = malloc(sizeof(struct desktopFileList));
    list->count = 0;
    list->size = 16;
    list->files = malloc(sizeof(struct desktopFile *) * 16);
    return list;
}


int parseDirectory(char *dirPath, struct desktopFileList *fileList) {
    DIR *d;
    struct dirent *dir;
    d = opendir(dirPath);
    if(d) {
        while ((dir = readdir(d)) != NULL) {
            if(dir->d_type == DT_REG) {
                char *lastDot = strrchr(dir->d_name, '.');
                if (lastDot && !strcmp(lastDot, ".desktop")) {
                    char *path = malloc(sizeof(char) * (strlen(dirPath) + strlen(dir->d_name) + 1));
                    strcpy(path, dirPath);
                    strcat(path, dir->d_name);

                    struct desktopFile *file = malloc(sizeof(struct desktopFile));
                    if(parseDesktopFile(path, file) == -1) {
                        free(path);
                        free(file);
                        continue;
                    }
                    if(fileList->size == fileList->count) {
                        fileList->files = realloc(fileList->files, sizeof(struct desktopFile *) * fileList->size * 2);
                        fileList->size *= 2;
                    }
                    fileList->files[fileList->count] = file;
                    fileList->count++;
                }
            }
        }
        closedir(d);
    }
    return 0;
}

struct desktopFileList *parseDFDirs(char *paths[], int pathCnt) {
    struct desktopFileList *DFList = initFileList();
    for(int path = 0; path < pathCnt; path++) {
        parseDirectory(paths[path], DFList);
    }
    return DFList;
}
