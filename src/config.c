#include "config.h"
#include "iniFileParsingUtils.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* DFA for keys: Columns, Rows, IconSize, HGap, VGap, FontSize, FontName, NameMaxLen, Terminal
 *
 *  State 1  : initial
 *  State 2  : after 'F'
 *  State 3  : after 'Fo'
 *  State 4  : after 'Fon'
 *  State 5  : after 'Font'
 *  State 6  : after 'T'
 *
 *  Terminal  key         ending to verify   case
 *  -1        Columns     "olumns"           0
 *  -2        Rows        "ows"              1
 *  -3        HGap        "Gap"              2
 *  -4        VGap        "Gap"              3
 *  -5        IconSize    "conSize"          4
 *  -6        FontSize    "ize"              5
 *  -7        FontName    "ame"              6
 *  -8        NameMaxLen  "ameMaxLen"        7
 *  -9        Terminal    "rminal"           8
 */
#define E(state,c,new) [state*128+(c)]=new,
static int configTT[128*7] = {
    E(1,'C',-1)  E(1,'R',-2)  E(1,'H',-3)  E(1,'V',-4)  E(1,'I',-5)  E(1,'F', 2)  E(1,'N',-8)  E(1,'T', 6)
    E(2,'o', 3)
    E(3,'n', 4)
    E(4,'t', 5)
    E(5,'S',-6)  E(5,'N',-7)
    E(6,'e',-9)
};
static char *configEnds[] = {
    "olumns",    /* 0: Columns    */
    "ows",       /* 1: Rows       */
    "Gap",       /* 2: HGap       */
    "Gap",       /* 3: VGap       */
    "conSize",   /* 4: IconSize   */
    "ize",       /* 5: FontSize   */
    "ame",       /* 6: FontName   */
    "ameMaxLen", /* 7: NameMaxLen */
    "rminal",    /* 8: Terminal   */
};

/* DFA for [PageN] and [FolderN] sections: Apps, Columns, Rows, Name, Icon
 *  State 1: initial   State 2: after 'C'   State 3: after 'I'
 *  -1: Apps    "pps"    -2: Columns "lumns"   -3: Rows  "ows"
 *  -4: Name    "ame"    -5: Icon    "on"
 */
static int sectionTT[128*4] = {
    E(1,'A',-1)  E(1,'C', 2)  E(1,'R',-3)  E(1,'N',-4)  E(1,'I', 3)
    E(2,'o',-2)
    E(3,'c',-5)
};
static char *sectionEnds[] = { "pps", "lumns", "ows", "ame", "on" };

typedef struct {
    LauncherConfig *cfg;
    int  pageColumns;
    int  pageRows;
    char displayName[256];
    char iconName[256];
} SectionParseState;

/* Returns true if the name looks like a folder reference: "folder" + one or more digits */
static bool is_folder_ref(const char *name) {
    if (strncmp(name, "folder", 6) != 0) return false;
    const char *r = name + 6;
    if (!*r) return false;
    for (; *r; r++) if (*r < '0' || *r > '9') return false;
    return true;
}

static void sectionHandler(FILE *file, int state, void *data) {
    SectionParseState *sps = data;
    LauncherConfig *cfg = sps->cfg;

    if (state == 1) { sps->pageColumns = intParser(file); return; }
    if (state == 2) { sps->pageRows    = intParser(file); return; }
    if (state == 3) {
        char *s = stringParser(file);
        if (s) { strncpy(sps->displayName, s, sizeof(sps->displayName)-1); free(s); }
        return;
    }
    if (state == 4) {
        char *s = stringParser(file);
        if (s) { strncpy(sps->iconName, s, sizeof(sps->iconName)-1); free(s); }
        return;
    }
    if (state != 0) return;

    /* Apps (case 0) */
    char *line = stringParser(file);
    if (!line || !*line) { free(line); return; }

    char *p = line;
    while (p) {
        char *comma = strchr(p, ',');
        if (comma) *comma = '\0';

        while (*p == ' ') p++;
        char *end = p + strlen(p);
        while (end > p && end[-1] == ' ') end--;
        *end = '\0';

        char *slot = NULL;
        if (*p) {
            if (is_folder_ref(p)) {
                slot = strdup(p); /* keep as-is; resolved in ui_init */
            } else {
                size_t len = strlen(p);
                if (len < 8 || strcmp(p + len - 8, ".desktop") != 0) {
                    slot = malloc(len + 9);
                    memcpy(slot, p, len);
                    memcpy(slot + len, ".desktop", 9);
                } else {
                    slot = strdup(p);
                }
            }
        }

        cfg->orderSlots = realloc(cfg->orderSlots,
            (cfg->orderSlotCount + 1) * sizeof(char *));
        cfg->orderSlots[cfg->orderSlotCount++] = slot;

        p = comma ? comma + 1 : NULL;
    }

    free(line);
}

static void configHandler(FILE *file, int state, void *data) {
    LauncherConfig *cfg = data;
    switch (state) {
        case 0: cfg->columns  = intParser(file); break;
        case 1: cfg->rows     = intParser(file); break;
        case 2: cfg->hGap     = intParser(file); break;
        case 3: cfg->vGap     = intParser(file); break;
        case 4: cfg->iconSize = intParser(file); break;
        case 5: cfg->fontSize    = intParser(file); break;
        case 6: {
            char *s = stringParser(file);
            if (s) { strncpy(cfg->fontName, s, sizeof(cfg->fontName) - 1); free(s); }
            break;
        }
        case 7: cfg->nameMaxLen = intParser(file); break;
        case 8: {
            char *s = stringParser(file);
            if (s) { strncpy(cfg->terminal, s, sizeof(cfg->terminal) - 1); free(s); }
            break;
        }
    }
}

void loadConfig(LauncherConfig *cfg) {
    cfg->columns         = 6;
    cfg->rows            = 4;
    cfg->iconSize        = 96;
    cfg->hGap            = 12;
    cfg->vGap            = 12;
    cfg->fontSize        = 12;
    cfg->nameMaxLen      = 20;
    cfg->orderSlots      = NULL;
    cfg->orderSlotCount  = 0;
    cfg->pageDefs        = NULL;
    cfg->pageDefCount    = 0;
    cfg->folderDefs      = NULL;
    cfg->folderDefCount  = 0;
    strncpy(cfg->fontName, "CaskaydiaCoveNerdFont-SemiBold", sizeof(cfg->fontName) - 1);
    strncpy(cfg->terminal, "kitty", sizeof(cfg->terminal) - 1);

    const char *home = getenv("HOME");
    if (!home) return;

    char path[512];
    snprintf(path, sizeof(path), "%s/.config/launcher.conf", home);
    FILE *f = fopen(path, "r");
    if (!f) return;

    int c;
    while ((c = fgetc(f)) != '[' && c != EOF);
    if (c == EOF) { fclose(f); return; }
    ungetc('[', f);

    struct iniSectionDef sd = {
        .sectionName = NULL,
        .transTable  = configTT,
        .endings     = (char **)configEnds,
        .caseHandler = configHandler,
        .data        = cfg,
    };
    int result = parseIniSection(f, &sd);
    free(sd.sectionName);

    /* Parse [PageN] and [FolderN] sections in whatever order they appear */
    while (result == 1) {
        int slotsBefore = cfg->orderSlotCount;
        SectionParseState sps = { .cfg = cfg, .pageColumns = 0, .pageRows = 0 };
        sps.displayName[0] = sps.iconName[0] = '\0';
        struct iniSectionDef sd2 = {
            .sectionName = NULL,
            .transTable  = sectionTT,
            .endings     = (char **)sectionEnds,
            .caseHandler = sectionHandler,
            .data        = &sps,
        };
        result = parseIniSection(f, &sd2);
        char *secName = sd2.sectionName;

        int pageCols    = sps.pageColumns > 0 ? sps.pageColumns : cfg->columns;
        int pageRows    = sps.pageRows    > 0 ? sps.pageRows    : cfg->rows;
        int appsPerPage = pageCols * pageRows;

        bool isFolder = secName && strncmp(secName, "folder", 6) == 0;
        bool isPage   = secName && strncmp(secName, "page",   4) == 0;

        if (isFolder) {
            int fCount = cfg->orderSlotCount - slotsBefore;

            cfg->folderDefs = realloc(cfg->folderDefs,
                (cfg->folderDefCount + 1) * sizeof(LauncherFolderDef));
            LauncherFolderDef *fd = &cfg->folderDefs[cfg->folderDefCount++];
            memset(fd, 0, sizeof(*fd));
            strncpy(fd->sectionName, secName, sizeof(fd->sectionName) - 1);
            strncpy(fd->displayName, sps.displayName, sizeof(fd->displayName) - 1);
            strncpy(fd->iconName,    sps.iconName,    sizeof(fd->iconName)    - 1);
            fd->columns = pageCols;
            fd->rows    = pageRows;

            /* Move freshly-added slots out of cfg->orderSlots into the folder */
            fd->orderSlots     = malloc((fCount > 0 ? fCount : 1) * sizeof(char *));
            fd->orderSlotCount = fCount;
            if (fCount > 0)
                memcpy(fd->orderSlots, cfg->orderSlots + slotsBefore,
                       fCount * sizeof(char *));
            cfg->orderSlotCount = slotsBefore;

            /* Pad/truncate folder slots to exactly one page */
            if (appsPerPage > 0) {
                while (fd->orderSlotCount > appsPerPage) {
                    fd->orderSlotCount--;
                    free(fd->orderSlots[fd->orderSlotCount]);
                }
                while (fd->orderSlotCount < appsPerPage) {
                    fd->orderSlots = realloc(fd->orderSlots,
                        (fd->orderSlotCount + 1) * sizeof(char *));
                    fd->orderSlots[fd->orderSlotCount++] = NULL;
                }
            }
        } else if (isPage) {
            cfg->pageDefs = realloc(cfg->pageDefs,
                (cfg->pageDefCount + 1) * sizeof(LauncherPageDef));
            cfg->pageDefs[cfg->pageDefCount++] = (LauncherPageDef){ pageCols, pageRows };

            if (appsPerPage > 0) {
                while (cfg->orderSlotCount > slotsBefore + appsPerPage) {
                    cfg->orderSlotCount--;
                    free(cfg->orderSlots[cfg->orderSlotCount]);
                }
                while (cfg->orderSlotCount < slotsBefore + appsPerPage) {
                    cfg->orderSlots = realloc(cfg->orderSlots,
                        (cfg->orderSlotCount + 1) * sizeof(char *));
                    cfg->orderSlots[cfg->orderSlotCount++] = NULL;
                }
            }
        } else {
            /* Unknown section — discard any slots it added */
            for (int i = slotsBefore; i < cfg->orderSlotCount; i++)
                free(cfg->orderSlots[i]);
            cfg->orderSlotCount = slotsBefore;
        }

        free(secName);
    }

    fclose(f);
}
