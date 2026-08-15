#pragma once

typedef struct {
    int columns; /* 0 = use global default */
    int rows;    /* 0 = use global default */
} LauncherPageDef;

typedef struct {
    char  sectionName[64];   /* "folder1", "folder2", … */
    char  displayName[256];  /* from Name= */
    char  iconName[256];     /* from Icon= */
    int   columns;
    int   rows;
    char **orderSlots;
    int   orderSlotCount;
} LauncherFolderDef;

typedef struct {
    int  columns;
    int  rows;
    int  iconSize;
    int  hGap;
    int  vGap;
    int  fontSize;
    int  nameMaxLen;
    char fontName[512];
    char terminal[512];
    /* Ordered slot list from [PageN] sections: NULL entry = blank slot. */
    char **orderSlots;
    int   orderSlotCount;
    /* Per-page dimension overrides, one entry per [PageN] section. */
    LauncherPageDef  *pageDefs;
    int               pageDefCount;
    /* Folder definitions, one entry per [folderN] section. */
    LauncherFolderDef *folderDefs;
    int                folderDefCount;
} LauncherConfig;

/* Loads ~/.config/launcher.conf; fills in defaults if the file is absent. */
void loadConfig(LauncherConfig *cfg);
