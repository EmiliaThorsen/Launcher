#include "iconFinder.h"
#include "iconIndexParser.h"
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static struct iconIndexFile **themes = NULL;
static char **themeNames = NULL;
static int themeCount = 0;
static int themeCapacity = 0;
static char *requestedTheme = NULL;

static const char *exts[] = {".png", ".svg", ".xpm", NULL};

static char *lookupFile(const char *dir, const char *name) {
    for (int i = 0; exts[i]; i++) {
        size_t dLen = strlen(dir), nLen = strlen(name), eLen = strlen(exts[i]);
        char *path = malloc(dLen + nLen + eLen + 1);
        memcpy(path, dir, dLen);
        memcpy(path + dLen, name, nLen);
        memcpy(path + dLen + nLen, exts[i], eLen + 1);
        if (access(path, F_OK) == 0) return path;
        free(path);
    }
    return NULL;
}

static int dirMatchesSize(struct iconIndexDir *d, int size, int scale) {
    if (d->Scale != scale) return 0;
    if (d->Type == 1) return d->Size == size;
    if (d->Type == 2) return d->MinSize <= size && size <= d->MaxSize;
    if (d->Type == 3) return (d->Size - d->Threshold) <= size && size <= (d->Size + d->Threshold);
    return 0;
}

static int dirSizeDist(struct iconIndexDir *d, int size, int scale) {
    if (dirMatchesSize(d, size, scale)) return 0;
    if (d->Type == 1) return abs(d->Size * d->Scale - size * scale);
    if (d->Type == 2) {
        if (size * scale < d->MinSize * d->Scale) return d->MinSize * d->Scale - size * scale;
        if (size * scale > d->MaxSize * d->Scale) return size * scale - d->MaxSize * d->Scale;
        return 0;
    }
    if (d->Type == 3) {
        if (size * scale < (d->Size - d->Threshold) * d->Scale)
            return (d->Size - d->Threshold) * d->Scale - size * scale;
        if (size * scale > (d->Size + d->Threshold) * d->Scale)
            return size * scale - (d->Size + d->Threshold) * d->Scale;
        return 0;
    }
    return INT_MAX;
}

/* Two-pass lookup per the freedesktop spec:
   pass 1 - exact size/scale match, pass 2 - closest size match. */
static char *lookupInTheme(struct iconIndexFile *theme, const char *name, int size, int scale) {
    for (int i = 0; i < theme->subDirCount; i++) {
        if (dirMatchesSize(theme->subDirs[i], size, scale)) {
            char *path = lookupFile(theme->subDirs[i]->path, name);
            if (path) return path;
        }
    }
    int minDist = INT_MAX;
    char *best = NULL;
    for (int i = 0; i < theme->subDirCount; i++) {
        int dist = dirSizeDist(theme->subDirs[i], size, scale);
        if (dist >= minDist) continue;
        char *path = lookupFile(theme->subDirs[i]->path, name);
        if (!path) continue;
        free(best);
        best = path;
        minDist = dist;
    }
    return best;
}

static int findThemeIdx(const char *name) {
    for (int i = 0; i < themeCount; i++)
        if (strcmp(themeNames[i], name) == 0) return i;
    return -1;
}

static int loadTheme(const char *name) {
    int existing = findThemeIdx(name);
    if (existing >= 0) return existing;

    char dirs[4][512];
    int ndirs = 0;
    const char *home = getenv("HOME");
    if (home) {
        snprintf(dirs[ndirs++], 512, "%s/.local/share/icons/", home);
        snprintf(dirs[ndirs++], 512, "%s/.icons/", home);
    }
    snprintf(dirs[ndirs++], 512, "/usr/local/share/icons/");
    snprintf(dirs[ndirs++], 512, "/usr/share/icons/");

    char path[4096];
    for (int i = 0; i < ndirs; i++) {
        snprintf(path, sizeof(path), "%s%s/index.theme", dirs[i], name);
        struct iconIndexFile *iif = malloc(sizeof(struct iconIndexFile));
        if (parseIconIndex(path, iif) != -1) {
            if (themeCount == themeCapacity) {
                themeCapacity = themeCapacity ? themeCapacity * 2 : 16;
                themes = realloc(themes, sizeof(*themes) * themeCapacity);
                themeNames = realloc(themeNames, sizeof(*themeNames) * themeCapacity);
            }
            themes[themeCount] = iif;
            themeNames[themeCount] = strdup(name);
            return themeCount++;
        }
        free(iif);
    }
    return -1;
}

/* Recurse through Inherits chain; depth cap prevents cycles. */
static char *findInThemeChain(const char *name, int size, int scale, const char *theme, int depth) {
    if (depth > 10) return NULL;
    int idx = loadTheme(theme);
    if (idx < 0) return NULL;

    char *result = lookupInTheme(themes[idx], name, size, scale);
    if (result) return result;

    if (!themes[idx]->Inherits) return NULL;
    char *copy = strdup(themes[idx]->Inherits);
    char *sp, *parent = strtok_r(copy, ",", &sp);
    while (parent) {
        while (*parent == ' ') parent++;
        result = findInThemeChain(name, size, scale, parent, depth + 1);
        if (result) { free(copy); return result; }
        parent = strtok_r(NULL, ",", &sp);
    }
    free(copy);
    return NULL;
}

/* Clone each subdir of themeIdx with basePath as the root instead of the original root. */
static void addThemeBasePath(int themeIdx, const char *basePath) {
    struct iconIndexFile *theme = themes[themeIdx];
    int origCount = theme->subDirCount;
    size_t baseLen = strlen(basePath);
    for (int i = 0; i < origCount; i++) {
        struct iconIndexDir *orig = theme->subDirs[i];
        size_t nameLen = strlen(orig->name);
        char *dirPath = malloc(baseLen + nameLen + 2);
        memcpy(dirPath, basePath, baseLen);
        memcpy(dirPath + baseLen, orig->name, nameLen);
        dirPath[baseLen + nameLen]     = '/';
        dirPath[baseLen + nameLen + 1] = '\0';

        struct iconIndexDir *clone = malloc(sizeof(*clone));
        *clone      = *orig;  /* copy size/type metadata */
        clone->name = orig->name; /* borrowed; not freed separately */
        clone->path = dirPath;

        if (theme->subDirCount == theme->subDirListSize) {
            theme->subDirListSize *= 2;
            theme->subDirs = realloc(theme->subDirs,
                sizeof(*theme->subDirs) * theme->subDirListSize);
        }
        theme->subDirs[theme->subDirCount++] = clone;
    }
}

void initIconFinder(const char *themeName) {
    themeCount = 0;
    themeCapacity = 16;
    themes = malloc(sizeof(*themes) * themeCapacity);
    themeNames = malloc(sizeof(*themeNames) * themeCapacity);
    free(requestedTheme);
    requestedTheme = strdup(themeName);
    loadTheme(themeName);

    /* Pre-load hicolor and extend it with user icon directories */
    int hicolorIdx = loadTheme("hicolor");
    if (hicolorIdx >= 0) {
        const char *home = getenv("HOME");
        if (home) {
            char p[512];
            snprintf(p, sizeof(p), "%s/.local/share/icons/hicolor/", home);
            addThemeBasePath(hicolorIdx, p);
            snprintf(p, sizeof(p), "%s/.icons/hicolor/", home);
            addThemeBasePath(hicolorIdx, p);
        }
    }
}

char *findIconPath(const char *iconName, int size, int scale) {
    char *result = findInThemeChain(iconName, size, scale, requestedTheme, 0);
    if (result) return result;

    if (strcmp(requestedTheme, "hicolor") != 0) {
        result = findInThemeChain(iconName, size, scale, "hicolor", 0);
        if (result) return result;
    }

    return lookupFile("/usr/share/pixmaps/", iconName);
}
