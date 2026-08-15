#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "iniFileParsingUtils.h"
#include "iconIndexParser.h"

#define E(state,c,new) [state*128+(c)]=new,
int indexStartTT[128*2] = {
E(1,'N',-1) //Name
E(1,'C',-2) //Comment
E(1,'I',-3) //Inherits
E(1,'D',-4) //Directories
E(1,'S',-5) //ScaledDirectories
E(1,'H',-6) //Hidden
E(1,'E',-7) //Example
};

char *indexStartEnds[] = {"ame", "omment","nherits","irectories","caledDirectories","idden","xample"};

void indexStartCaseHandler(FILE *file, int state, void *in) {
    struct iconIndexFile *iif = in;
    switch (state) {
        case 0:  iif->Name = stringParser(file); break;
        case 1:  iif->Comment = stringParser(file); break;
        case 2:  iif->Inherits = stringParser(file); break;
        case 3:  iif->Directories = stringParser(file); break;
        case 4:  iif->ScaledDirectories = stringParser(file); break;
        case 5:  iif->Hidden = boolParser(file); break;
        case 6:  iif->Example = stringParser(file); break;
    }
}

int indexDirKeyTT[128*5] = {
E(1,'S',2)  E(2,'i',-1) //Size
            E(2,'c',-2) //Scale
E(1,'C',-3)             //Context
E(1,'M',3)  E(3,'a',-5) //MaxSize
            E(3,'i',-6) //MinSize
E(1,'T',4)  E(4,'y',-4) //Type
            E(4,'h',-7) //Threshold
};
char *indexDirKeyEnds[] = {"ze", "ale", "ontext", "pe", "xSize", "nSize", "reshold"};


void indexDirKeyCaseHandler(FILE *file, int state, void *in) {
    struct iconIndexDir *iid = in;
    switch (state) {
        case 0: iid->Size = intParser(file); break;
        case 1: iid->Scale = intParser(file); break;
        case 2: iid->Context = stringParser(file); break;
        case 3: iid->Type = iconSizeTypeParser(file); break;  /* DFA -4 → case 3 */
        case 4: iid->MaxSize = intParser(file); break;         /* DFA -5 → case 4 */
        case 5: iid->MinSize = intParser(file); break;         /* DFA -6 → case 5 */
        case 6: iid->Threshold = intParser(file); break;
    }
}

int parseIconIndex(char *fileName, struct iconIndexFile *iif) {
    FILE *file = fopen(fileName, "r");
    if(file == NULL) return -1;

    memset(iif, 0, sizeof(*iif));

    int c;
    while ((c = fgetc(file)) != '[' && c != EOF);
    if (c == EOF) { fclose(file); return -1; }
    ungetc('[', file);

    struct iniSectionDef isd = {
        .caseHandler = indexStartCaseHandler,
        .data = iif,
        .endings = (char **)indexStartEnds,
        .transTable = indexStartTT
    };
    int result = parseIniSection(file, &isd);
    if (result == -1) { fclose(file); return -1; }
    if (strcmp(isd.sectionName, "Icon Theme") != 0) { fclose(file); return -1; }

    /* Base dir is everything up to and including the last '/' in fileName.
       Subdir paths are constructed as: baseDir + subDirName + "/" */
    const char *lastSlash = strrchr(fileName, '/');
    int baseLen = lastSlash ? (int)(lastSlash - fileName) + 1 : 0;

    iif->subDirs = malloc(sizeof(struct iconIndexDir *) * 16);
    iif->subDirCount = 0;
    iif->subDirListSize = 16;

    while(1) {
        struct iconIndexDir *iid = malloc(sizeof(struct iconIndexDir));
        iid->Scale = 1;
        iid->MinSize = -1;
        iid->MaxSize = -1;
        iid->Threshold = 2;
        iid->Type = 3; /* default: Threshold per spec */

        struct iniSectionDef dsd = {
            .caseHandler = indexDirKeyCaseHandler,
            .data = iid,
            .endings = (char **)indexDirKeyEnds,
            .transTable = indexDirKeyTT
        };
        result = parseIniSection(file, &dsd);
        if(result == -1) { free(iid); break; }

        iid->name = dsd.sectionName;
        if(iid->MinSize == -1) iid->MinSize = iid->Size;
        if(iid->MaxSize == -1) iid->MaxSize = iid->Size;

        size_t nameLen = strlen(iid->name);
        char *dirPath = malloc(baseLen + nameLen + 2);
        memcpy(dirPath, fileName, baseLen);
        memcpy(dirPath + baseLen, iid->name, nameLen);
        dirPath[baseLen + nameLen] = '/';
        dirPath[baseLen + nameLen + 1] = '\0';
        iid->path = dirPath;

        if(iif->subDirCount == iif->subDirListSize) {
            iif->subDirs = realloc(iif->subDirs, sizeof(struct iconIndexDir *) * iif->subDirListSize * 2);
            iif->subDirListSize *= 2;
        }
        iif->subDirs[iif->subDirCount++] = iid;

        if(feof(file) || ferror(file)) break;
    }

    fclose(file);
    return 0;
}
