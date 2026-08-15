#include <stdio.h>


struct iniSectionDef {
    char *sectionName;
    void (*caseHandler)(FILE *, int, void *);
    void *data;
    int *transTable;
    char **endings;
};


int boolParser(FILE *file);
char* stringParser(FILE *file);
int typeParser(FILE *file);
int intParser(FILE *file);
int iconSizeTypeParser(FILE *file);
int parseIniSection(FILE *file, struct iniSectionDef *isd);
