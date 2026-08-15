#include <stdio.h>
#include <stdlib.h>
#include "iniFileParsingUtils.h"

static char lastRead;
#define fgetc(file) (lastRead=fgetc(file))


int boolParser(FILE *file) {
    switch(fgetc(file)) {
        case 't': if(fgetc(file) == 'r' && fgetc(file) == 'u' && fgetc(file) == 'e') return 1;
        break;
        case 'f': if(fgetc(file) == 'a' && fgetc(file) == 'l' && fgetc(file) == 's' && fgetc(file) == 'e') return 0;
        break;
    }
    return -1;
}


int _notNLorEoF(FILE *file) {
    if(fgetc(file) == '\n') return 0;
    if(feof(file) || ferror(file)) return 0;
    return 1;
}


char* stringParser(FILE *file) { //for now just pre seek the entire string, alloc exactly what is needed and naively copy, no array handing or locales
    fpos_t start;
    fgetpos(file, &start);
    int chars = 0;
    while (_notNLorEoF(file)) chars++;
    char *string = malloc((chars + 1) * sizeof(char));
    fsetpos(file, &start);
    for(int i = 0; i < chars; i++) {
        string[i] = getc(file);
    }
    string[chars] = '\0';
    return string;
}


int typeParser(FILE *file) {
    int index = 0;
    char ch;
    switch (fgetc(file)) {
        case 'A':
            while(  (ch = "pplication"[index++]) != '\0') if(fgetc(file)!=ch) return -1;
            return 1;
        break;
        case 'L':
            while((ch = "ink"[index++]) != '\0') if(fgetc(file)!=ch) return -1;
            return 2;
        break;
        case 'D':
            while((ch = "irectory"[index++]) != '\0') if(fgetc(file)!=ch) return -1;
            return 3;
        break;
    }
    return -1;
}

int iconSizeTypeParser(FILE *file) {
    int index = 0;
    char ch;
    switch (fgetc(file)) {
        case 'F':
            while(  (ch = "ixed"[index++]) != '\0') if(fgetc(file)!=ch) return -1;
            return 1;
        break;
        case 'S':
            while((ch = "calable"[index++]) != '\0') if(fgetc(file)!=ch) return -1;
            return 2;
        break;
        case 'T':
            while((ch = "hreshold"[index++]) != '\0') if(fgetc(file)!=ch) return -1;
            return 3;
        break;
    }
    return -1;
}

int intParser(FILE *file) {
    int acum = 0;
    while (1) {
        int ch = fgetc(file);  /* macro — keeps lastRead current so invalid: fallthrough works */
        if(ch == '\n' || feof(file) || ferror(file)) return acum;
        if(ch < '0' || ch > '9') return -1;
        acum = acum * 10 + (ch - '0');
    }
}

int parseIniSection(FILE *file, struct iniSectionDef *isd) {
    if(fgetc(file) != '[') return -1; //read section name
    fpos_t start;
    fgetpos(file, &start);
    int chars = 0;
    while (fgetc(file) != '\n') chars++;
    char *string = malloc(chars * sizeof(char));
    fsetpos(file, &start);
    for(int i = 0; i < chars; i++) string[i] = getc(file); //not verifying section caracter set validity rn
    if(string[chars - 1] != ']') return -1;
    string[chars - 1] = '\0';
    isd->sectionName = string;

    while(1) { //start parsing section data
        int state = 1;
        /* skip leading whitespace/indentation on each line */
        int nextChar;
        while ((nextChar = fgetc(file)) == ' ' || nextChar == '\t');
        if(nextChar > 127) goto invalid;
        state = isd->transTable[state*128+nextChar];
        while (state > 0) { //automata to narrow down zoomingly
            nextChar = fgetc(file);
            if(nextChar > 127) goto invalid; //fucking utf-8
            state = isd->transTable[state*128+nextChar];
        }
        if(state == 0) goto invalid;

        //validate rest of key
        int search = 0;
        char validNext;
        while ((validNext = isd->endings[-state - 1][search++]) != '\0' ) {
            if(fgetc(file) != validNext) goto invalid;
        }

        /* locale modifier [lang] on a known key — skip the whole line */
        if(fgetc(file) == '[') {
            while(_notNLorEoF(file));
            if(feof(file) || ferror(file)) return 0;
            continue;
        }
        ungetc(lastRead, file);

        //consume equal sign with potential space
        while(fgetc(file) == ' ');
        if(lastRead != '=') goto invalid;
        while(fgetc(file) == ' ');
        ungetc(lastRead, file);

        isd->caseHandler(file,-state - 1, isd->data);

        invalid:
        if(lastRead == '[') { //start of new section
            ungetc('[', file);
            return 1;
        }
        if(lastRead == '\n') continue;
        while(_notNLorEoF(file));
        if(feof(file) || ferror(file)) return 0;
    }
}

