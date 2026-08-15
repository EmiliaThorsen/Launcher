#include "ui.h"
#include "desktopFileParser.h"
#include "iconFinder.h"
#include <png.h>
#include <librsvg/rsvg.h>
#include <cairo.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>

#define MAX_APPS    512
#define MAX_PAGES   256
#define MAX_FOLDERS  32
#define ARROW_W      56

typedef struct { int startIdx; int cols; int rows; } PageInfo;

typedef struct {
    char name[128];
    char exec[512];
    char desktopFilename[256];
    int  needsTerminal;
    int  blank;
    int  iconFailed;
    int  isFolder;
    int  folderIdx;
    SDL_Texture *icon;
} AppEntry;

typedef struct {
    char        sectionName[64];
    char        name[256];
    SDL_Texture *icon;
    AppEntry   *apps;   /* dynamically allocated, length = cols*rows */
    int         appCount;
    int         cols;
    int         rows;
    PageInfo    page;   /* always { .startIdx=0, .cols=cols, .rows=rows } */
} UIFolder;

static AppEntry apps[MAX_APPS];
static int appCount       = 0;
static int cfg_cols       = 6;
static int cfg_rows       = 4;
static int cfg_iconSize   = 96;
static int cfg_hGap       = 12;
static int cfg_vGap       = 12;
static int cfg_fontSize   = 12;
static int cfg_nameMaxLen = 20;
static int  currentPage   = 0;
static int  selectedIdx   = -1;
static int  hoverIdx      = -1;
static bool pendingLaunch = false;
static char cfg_terminal[512] = "kitty";
static SDL_Texture *errorIcon = NULL;

static PageInfo pageInfos[MAX_PAGES];
static int      pageInfoCount = 0;

static UIFolder uiFolders[MAX_FOLDERS];
static int      uiFolderCount    = 0;
static int      currentFolderIdx = -1; /* -1 = main screen */
static int      savedPage        = 0;
static int      savedSelected    = -1;

/* Load a PNG file into an SDL texture. Returns NULL for non-PNG or on error. */
static SDL_Texture *load_png_texture(SDL_Renderer *renderer, const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext || strcmp(ext, ".png") != 0) return NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    unsigned char sig[8];
    if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8)) { fclose(fp); return NULL; }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return NULL; }

    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return NULL; }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    int w = (int)png_get_image_width(png, info);
    int h = (int)png_get_image_height(png, info);
    png_byte ctype = png_get_color_type(png, info);
    png_byte depth = png_get_bit_depth(png, info);

    if (depth == 16)  png_set_strip_16(png);
    if (ctype == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (ctype == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (ctype == PNG_COLOR_TYPE_RGB   || ctype == PNG_COLOR_TYPE_GRAY ||
        ctype == PNG_COLOR_TYPE_PALETTE) png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (ctype == PNG_COLOR_TYPE_GRAY  || ctype == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    png_set_interlace_handling(png);
    png_read_update_info(png, info);

    int rowbytes = (int)png_get_rowbytes(png, info);
    unsigned char *pixels = malloc((size_t)(h * rowbytes));
    if (!pixels) { png_destroy_read_struct(&png, &info, NULL); fclose(fp); return NULL; }

    png_bytep *rows = malloc((size_t)h * sizeof(png_bytep));
    if (!rows) { free(pixels); png_destroy_read_struct(&png, &info, NULL); fclose(fp); return NULL; }
    for (int y = 0; y < h; y++) rows[y] = pixels + y * rowbytes;
    png_read_image(png, rows);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    SDL_Surface *surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, rowbytes);
    SDL_Texture *tex = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
    if (surf) SDL_DestroySurface(surf);
    free(pixels);
    return tex;
}

static SDL_Texture *load_svg_texture(SDL_Renderer *renderer, const char *path, int size) {
    RsvgHandle *handle = rsvg_handle_new_from_file(path, NULL);
    if (!handle) return NULL;

    cairo_surface_t *cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t *cr = cairo_create(cs);
    RsvgRectangle vp = { 0, 0, size, size };
    rsvg_handle_render_document(handle, cr, &vp, NULL);
    cairo_destroy(cr);
    g_object_unref(handle);

    cairo_surface_flush(cs);
    unsigned char *data = cairo_image_surface_get_data(cs);
    int stride = cairo_image_surface_get_stride(cs);

    /* Cairo ARGB32 is premultiplied alpha; un-premultiply so SDL renders correctly */
    for (int y = 0; y < size; y++) {
        uint32_t *row = (uint32_t *)(data + y * stride);
        for (int x = 0; x < size; x++) {
            uint32_t p = row[x];
            uint8_t a = p >> 24;
            if (a > 0 && a < 255) {
                row[x] = ((uint32_t)a << 24)
                        | (((p >> 16 & 0xFF) * 255u / a) << 16)
                        | (((p >>  8 & 0xFF) * 255u / a) <<  8)
                        |  ((p       & 0xFF) * 255u / a);
            }
        }
    }

    SDL_Surface *surf = SDL_CreateSurfaceFrom(size, size, SDL_PIXELFORMAT_ARGB8888, data, stride);
    SDL_Texture *tex = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
    if (surf) SDL_DestroySurface(surf);
    cairo_surface_destroy(cs);
    return tex;
}

static SDL_Texture *load_icon_texture(SDL_Renderer *renderer, const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext && strcmp(ext, ".svg") == 0)
        return load_svg_texture(renderer, path, cfg_iconSize);
    return load_png_texture(renderer, path);
}

/* Returns true for strings like "folder1", "folder2", … */
static bool is_folder_ref(const char *s) {
    if (!s || strncmp(s, "folder", 6) != 0) return false;
    const char *r = s + 6;
    if (!*r) return false;
    for (; *r; r++) if (*r < '0' || *r > '9') return false;
    return true;
}

/* Resolve active apps/pages depending on whether we're in a folder */
#define ACTIVE_APPS       (currentFolderIdx < 0 ? apps       : uiFolders[currentFolderIdx].apps)
#define ACTIVE_APP_COUNT  (currentFolderIdx < 0 ? appCount   : uiFolders[currentFolderIdx].appCount)
#define ACTIVE_PAGE_INFO  (currentFolderIdx < 0 ? pageInfos  : &uiFolders[currentFolderIdx].page)
#define ACTIVE_PAGE_COUNT (currentFolderIdx < 0 ? pageInfoCount : 1)

static const Clay_Color COL_BG        = {24,  24,  28,  255};
static const Clay_Color COL_ITEM      = {44,  44,  52,  255};
static const Clay_Color COL_NO_ICON   = {60,  60,  76,  255};
static const Clay_Color COL_TEXT      = {210, 210, 215, 255};
static const Clay_Color COL_ARROW     = {50,  50,  65,  220};
static const Clay_Color COL_INVISIBLE = {0,   0,   0,   0  };

static void strip_field_codes(char *exec) {
    char *src = exec, *dst = exec;
    while (*src) {
        if (*src == '%') {
            src++;
            if (*src == '%') { *dst++ = '%'; src++; }  /* %% → literal % */
            else if (*src)   { src++; }                 /* %x  → drop both */
        } else {
            *dst++ = *src++;
        }
    }
    while (dst > exec && *(dst - 1) == ' ') dst--;
    *dst = '\0';
}

static void enter_folder(int idx) {
    if (idx < 0 || idx >= uiFolderCount) return;
    savedPage       = currentPage;
    savedSelected   = selectedIdx;
    currentFolderIdx = idx;
    currentPage     = 0;
    selectedIdx     = 0;
    hoverIdx        = -1;
}

bool ui_exit_folder(void) {
    if (currentFolderIdx < 0) return false;
    currentFolderIdx = -1;
    currentPage      = savedPage;
    selectedIdx      = savedSelected;
    hoverIdx         = -1;
    return true;
}

void ui_init(SDL_Renderer *renderer, const LauncherConfig *cfg) {
    cfg_cols       = cfg->columns;
    cfg_rows       = cfg->rows;
    cfg_iconSize   = cfg->iconSize;
    cfg_hGap       = cfg->hGap;
    cfg_vGap       = cfg->vGap;
    cfg_fontSize   = cfg->fontSize;
    cfg_nameMaxLen = cfg->nameMaxLen;
    strncpy(cfg_terminal, cfg->terminal, sizeof(cfg_terminal) - 1);
    initIconFinder("Adwaita");

    char userAppsDir[512] = "";
    const char *home = getenv("HOME");
    if (home) snprintf(userAppsDir, sizeof(userAppsDir),
                       "%s/.local/share/applications/", home);

    char *dirs[2] = { "/usr/share/applications/", NULL };
    int   ndirs   = 1;
    if (userAppsDir[0]) { dirs[ndirs++] = userAppsDir; }

    struct desktopFileList *list = parseDFDirs(dirs, ndirs);

    for (int i = 0; i < list->count && appCount < MAX_APPS; i++) {
        struct desktopFile *df = list->files[i];
        if (df->count == 0) continue;
        struct desktopGroup *g = df->groups[0];
        if (!g->Name || g->NoDisplay == 1 || g->Hidden == 1 || g->Type != 1) continue;

        AppEntry *app = &apps[appCount++];
        memset(app, 0, sizeof(*app));
        strncpy(app->name, g->Name, sizeof(app->name) - 1);

        app->needsTerminal = (g->Terminal == 1);
        if (g->Exec) {
            strncpy(app->exec, g->Exec, sizeof(app->exec) - 1);
            strip_field_codes(app->exec);
        }

        const char *slash = strrchr(df->filename, '/');
        strncpy(app->desktopFilename, slash ? slash + 1 : df->filename,
                sizeof(app->desktopFilename) - 1);

        /* Truncate long names in place */
        if (cfg_nameMaxLen > 0 && cfg_nameMaxLen < (int)(sizeof(app->name) - 1)) {
            int len = (int)strlen(app->name);
            if (len > cfg_nameMaxLen) {
                int keep = cfg_nameMaxLen - 3;
                if (keep < 0) keep = 0;
                app->name[keep]     = '.';
                app->name[keep + 1] = '.';
                app->name[keep + 2] = '.';
                app->name[keep + 3] = '\0';
            }
        }

        app->icon = NULL;
        if (g->Icon) {
            char *path = findIconPath(g->Icon, cfg_iconSize, 1);
            if (path) {
                app->icon = load_icon_texture(renderer, path);
                free(path);
            }
            if (!app->icon) app->iconFailed = 1;
        }
    }

    /* Build UIFolders now while all loaded apps are still in apps[] */
    uiFolderCount = 0;
    for (int f = 0; f < cfg->folderDefCount && uiFolderCount < MAX_FOLDERS; f++) {
        LauncherFolderDef *fd = &cfg->folderDefs[f];
        UIFolder *uf = &uiFolders[uiFolderCount];

        memset(uf, 0, sizeof(*uf));
        strncpy(uf->sectionName, fd->sectionName, sizeof(uf->sectionName) - 1);
        strncpy(uf->name,
                fd->displayName[0] ? fd->displayName : fd->sectionName,
                sizeof(uf->name) - 1);
        uf->cols = fd->columns > 0 ? fd->columns : cfg_cols;
        uf->rows = fd->rows    > 0 ? fd->rows    : cfg_rows;
        uf->page = (PageInfo){ 0, uf->cols, uf->rows };

        uf->icon = NULL;
        if (fd->iconName[0]) {
            char *p = findIconPath(fd->iconName, cfg_iconSize, 1);
            if (p) { uf->icon = load_icon_texture(renderer, p); free(p); }
        }
        if (!uf->icon) {
            const char *fb[] = { "folder", "inode-directory", NULL };
            for (int i = 0; fb[i] && !uf->icon; i++) {
                char *p = findIconPath(fb[i], cfg_iconSize, 1);
                if (p) { uf->icon = load_icon_texture(renderer, p); free(p); }
            }
        }

        int capacity = uf->cols * uf->rows;
        if (capacity <= 0) capacity = 1;
        uf->apps = calloc(capacity, sizeof(AppEntry));
        uf->appCount = 0;

        for (int s = 0; s < fd->orderSlotCount && uf->appCount < capacity; s++) {
            AppEntry blank_e; memset(&blank_e, 0, sizeof(blank_e)); blank_e.blank = 1;
            if (!fd->orderSlots[s]) {
                uf->apps[uf->appCount++] = blank_e;
            } else {
                bool found = false;
                for (int i = 0; i < appCount; i++) {
                    if (apps[i].blank || apps[i].isFolder) continue;
                    if (strcmp(apps[i].desktopFilename, fd->orderSlots[s]) == 0) {
                        uf->apps[uf->appCount++] = apps[i];
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    AppEntry missing; memset(&missing, 0, sizeof(missing));
                    missing.iconFailed = 1;
                    const char *slot = fd->orderSlots[s];
                    size_t slen = strlen(slot);
                    size_t nlen = (slen > 8 && strcmp(slot+slen-8, ".desktop")==0)
                                  ? slen-8 : slen;
                    if (nlen >= sizeof(missing.name)) nlen = sizeof(missing.name)-1;
                    memcpy(missing.name, slot, nlen); missing.name[nlen] = '\0';
                    uf->apps[uf->appCount++] = missing;
                }
            }
        }
        while (uf->appCount < capacity) {
            AppEntry blank_e; memset(&blank_e, 0, sizeof(blank_e)); blank_e.blank = 1;
            uf->apps[uf->appCount++] = blank_e;
        }

        uiFolderCount++;
    }

    /* Apply order from [PageN] sections; also exclude folder-referenced apps from auto-pages */
    if (cfg->orderSlotCount > 0 || cfg->folderDefCount > 0) {
        static AppEntry tmp[MAX_APPS];
        static bool     used[MAX_APPS];
        memset(used, 0, sizeof(bool) * appCount);
        int n = 0;

        for (int s = 0; s < cfg->orderSlotCount && n < MAX_APPS; s++) {
            AppEntry blank_entry;
            memset(&blank_entry, 0, sizeof(blank_entry));
            blank_entry.blank = 1;

            if (!cfg->orderSlots[s]) {
                tmp[n++] = blank_entry;
            } else if (is_folder_ref(cfg->orderSlots[s])) {
                /* Folder reference — resolve to a UIFolder index (assigned later) */
                AppEntry fe;
                memset(&fe, 0, sizeof(fe));
                fe.isFolder = 1;
                fe.folderIdx = -1; /* patched after uiFolders are built */
                /* Store the section name so we can match it */
                strncpy(fe.desktopFilename, cfg->orderSlots[s],
                        sizeof(fe.desktopFilename) - 1);
                tmp[n++] = fe;
            } else {
                bool found = false;
                for (int i = 0; i < appCount; i++) {
                    if (!used[i] && strcmp(apps[i].desktopFilename, cfg->orderSlots[s]) == 0) {
                        tmp[n++] = apps[i];
                        used[i] = true;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    AppEntry missing;
                    memset(&missing, 0, sizeof(missing));
                    missing.iconFailed = 1;
                    const char *slot = cfg->orderSlots[s];
                    size_t slen = strlen(slot);
                    size_t nlen = (slen > 8 && strcmp(slot + slen - 8, ".desktop") == 0)
                                  ? slen - 8 : slen;
                    if (nlen >= sizeof(missing.name)) nlen = sizeof(missing.name) - 1;
                    memcpy(missing.name, slot, nlen);
                    missing.name[nlen] = '\0';
                    tmp[n++] = missing;
                }
            }
        }

        /* Mark apps referenced by any folder so they are excluded from auto-pages */
        for (int f = 0; f < cfg->folderDefCount; f++) {
            for (int s = 0; s < cfg->folderDefs[f].orderSlotCount; s++) {
                const char *slot = cfg->folderDefs[f].orderSlots[s];
                if (!slot) continue;
                for (int i = 0; i < appCount; i++) {
                    if (!used[i] && strcmp(apps[i].desktopFilename, slot) == 0) {
                        used[i] = true;
                        break;
                    }
                }
            }
        }

        /* Remaining apps not in any page or folder */
        for (int i = 0; i < appCount; i++) {
            if (!used[i] && n < MAX_APPS) tmp[n++] = apps[i];
        }

        memcpy(apps, tmp, n * sizeof(AppEntry));
        appCount = n;
    }

    /* Load fallback icon for apps whose icon failed to resolve */
    const char *errorNames[] = { "image-missing", "dialog-error", "error", NULL };
    for (int i = 0; errorNames[i] && !errorIcon; i++) {
        char *path = findIconPath(errorNames[i], cfg_iconSize, 1);
        if (path) { errorIcon = load_icon_texture(renderer, path); free(path); }
    }

    /* Pad unordered tail to a full page so the last auto-page isn't partial */
    if (cfg_cols > 0 && cfg_rows > 0) {
        int lastPageSize = cfg_cols * cfg_rows;
        int tail    = appCount - cfg->orderSlotCount;
        int partial = tail % lastPageSize;
        if (partial != 0) {
            int toAdd = lastPageSize - partial;
            AppEntry blank_e;
            memset(&blank_e, 0, sizeof(blank_e));
            blank_e.blank = 1;
            while (toAdd-- > 0 && appCount < MAX_APPS)
                apps[appCount++] = blank_e;
        }
    }

    /* Build per-page boundary table from pageDefs + auto-pages for the rest */
    pageInfoCount = 0;
    int idx = 0;
    for (int p = 0; p < cfg->pageDefCount && pageInfoCount < MAX_PAGES; p++) {
        pageInfos[pageInfoCount++] = (PageInfo){
            idx,
            cfg->pageDefs[p].columns,
            cfg->pageDefs[p].rows
        };
        idx += cfg->pageDefs[p].columns * cfg->pageDefs[p].rows;
    }
    while (idx < appCount && pageInfoCount < MAX_PAGES) {
        pageInfos[pageInfoCount++] = (PageInfo){ idx, cfg_cols, cfg_rows };
        idx += cfg_cols * cfg_rows;
    }
    if (pageInfoCount == 0)
        pageInfos[pageInfoCount++] = (PageInfo){ 0, cfg_cols, cfg_rows };

    /* Patch folderIdx and name/icon for folder AppEntry objects in the main list */
    for (int i = 0; i < appCount; i++) {
        if (!apps[i].isFolder) continue;
        const char *sec = apps[i].desktopFilename;
        for (int f = 0; f < uiFolderCount; f++) {
            if (strcmp(uiFolders[f].sectionName, sec) == 0) {
                apps[i].folderIdx = f;
                strncpy(apps[i].name, uiFolders[f].name, sizeof(apps[i].name)-1);
                apps[i].icon = uiFolders[f].icon;
                break;
            }
        }
        if (apps[i].folderIdx < 0) {
            /* folder section referenced but not defined — show as blank */
            apps[i].blank    = 1;
            apps[i].isFolder = 0;
        }
    }

    selectedIdx = (appCount > 0) ? 0 : -1;
}

void ui_navigate_grid(int dx, int dy) {
    PageInfo *pages    = ACTIVE_PAGE_INFO;
    int       pageCnt  = ACTIVE_PAGE_COUNT;
    int       aCnt     = ACTIVE_APP_COUNT;
    if (pageCnt == 0) return;

    if (selectedIdx < 0) {
        selectedIdx = pages[currentPage].startIdx;
        return;
    }

    int page = currentPage;
    for (int p = 0; p < pageCnt; p++) {
        int sz = pages[p].cols * pages[p].rows;
        if (selectedIdx >= pages[p].startIdx &&
            selectedIdx <  pages[p].startIdx + sz) {
            page = p;
            break;
        }
    }

    int cols = pages[page].cols;
    int pos  = selectedIdx - pages[page].startIdx;
    int row  = pos / cols;
    int col  = pos % cols;

    col += dx;
    row += dy;

    if (col < 0) {
        if (page > 0) { page--; col = pages[page].cols - 1; }
        else col = 0;
    } else if (col >= cols) {
        if (page < pageCnt - 1) { page++; col = 0; }
        else col = cols - 1;
    }

    if (row < 0) row = 0;
    if (row >= pages[page].rows) row = pages[page].rows - 1;

    currentPage = page;
    int newIdx = pages[page].startIdx + row * pages[page].cols + col;
    if (newIdx >= aCnt) newIdx = aCnt - 1;
    if (newIdx < 0) newIdx = 0;
    selectedIdx = newIdx;
}

bool ui_launch_selected(void) {
    AppEntry *aApps = ACTIVE_APPS;
    int       aCnt  = ACTIVE_APP_COUNT;
    if (selectedIdx < 0 || selectedIdx >= aCnt) return false;
    AppEntry *app = &aApps[selectedIdx];

    if (app->isFolder) {
        enter_folder(app->folderIdx);
        return false;
    }
    if (!app->exec[0]) return false;

    pid_t pid = fork();
    if (pid != 0) return true;

    setsid();
    if (app->needsTerminal && cfg_terminal[0]) {
        char *args[] = { cfg_terminal, (char *)"-e", (char *)"sh", (char *)"-c", app->exec, NULL };
        execvp(cfg_terminal, args);
    } else {
        char *args[] = { (char *)"sh", (char *)"-c", app->exec, NULL };
        execvp("sh", args);
    }
    _exit(1);
}

bool ui_pop_launch(void) {
    bool v = pendingLaunch;
    pendingLaunch = false;
    return v;
}

Clay_RenderCommandArray ui_layout(bool clicked) {
    AppEntry *aApps    = ACTIVE_APPS;
    int       aCnt     = ACTIVE_APP_COUNT;
    PageInfo *aPages   = ACTIVE_PAGE_INFO;
    int       aPageCnt = ACTIVE_PAGE_COUNT;

    bool inFolder = (currentFolderIdx >= 0);
    int totalPages = aPageCnt > 0 ? aPageCnt : 1;
    if (currentPage >= totalPages) currentPage = totalPages - 1;

    int pageCols  = aPages[currentPage].cols;
    int pageRows  = aPages[currentPage].rows;
    int pageStart = aPages[currentPage].startIdx;
    int pageEnd   = pageStart + pageCols * pageRows;
    if (pageEnd > aCnt) pageEnd = aCnt;

    /* Handle clicks using previous frame's element positions */
    if (clicked) {
        if (inFolder && Clay_PointerOver(CLAY_ID("BackBtn"))) {
            ui_exit_folder();
            /* refresh context after exit */
            inFolder  = false;
            aApps     = ACTIVE_APPS;
            aCnt      = ACTIVE_APP_COUNT;
            aPages    = ACTIVE_PAGE_INFO;
            aPageCnt  = ACTIVE_PAGE_COUNT;
            totalPages = aPageCnt > 0 ? aPageCnt : 1;
            if (currentPage >= totalPages) currentPage = totalPages - 1;
            pageCols  = aPages[currentPage].cols;
            pageRows  = aPages[currentPage].rows;
            pageStart = aPages[currentPage].startIdx;
            pageEnd   = pageStart + pageCols * pageRows;
            if (pageEnd > aCnt) pageEnd = aCnt;
        } else if (!inFolder && Clay_PointerOver(CLAY_ID("PrevPage")) && currentPage > 0) {
            currentPage--;
            selectedIdx = aPages[currentPage].startIdx;
            pageCols  = aPages[currentPage].cols;
            pageRows  = aPages[currentPage].rows;
            pageStart = aPages[currentPage].startIdx;
            pageEnd   = pageStart + pageCols * pageRows;
            if (pageEnd > aCnt) pageEnd = aCnt;
        } else if (!inFolder && Clay_PointerOver(CLAY_ID("NextPage")) && currentPage < totalPages - 1) {
            currentPage++;
            selectedIdx = aPages[currentPage].startIdx;
            pageCols  = aPages[currentPage].cols;
            pageRows  = aPages[currentPage].rows;
            pageStart = aPages[currentPage].startIdx;
            pageEnd   = pageStart + pageCols * pageRows;
            if (pageEnd > aCnt) pageEnd = aCnt;
        } else {
            for (int i = pageStart; i < pageEnd; i++) {
                if (aApps[i].blank) continue;
                if (Clay_PointerOver(CLAY_IDI("Item", i))) {
                    selectedIdx = i;
                    if (aApps[i].isFolder) enter_folder(aApps[i].folderIdx);
                    else pendingLaunch = true;
                    break;
                }
            }
        }
    }

    /* Refresh context in case we just entered a folder via click */
    aApps     = ACTIVE_APPS;
    aCnt      = ACTIVE_APP_COUNT;
    aPages    = ACTIVE_PAGE_INFO;
    aPageCnt  = ACTIVE_PAGE_COUNT;
    inFolder  = (currentFolderIdx >= 0);
    totalPages = aPageCnt > 0 ? aPageCnt : 1;
    if (currentPage >= totalPages) currentPage = totalPages - 1;
    pageCols  = aPages[currentPage].cols;
    pageRows  = aPages[currentPage].rows;
    pageStart = aPages[currentPage].startIdx;
    pageEnd   = pageStart + pageCols * pageRows;
    if (pageEnd > aCnt) pageEnd = aCnt;

    /* Update selection only when mouse enters a new item (including blank slots) */
    int newHover = -1;
    for (int i = pageStart; i < pageEnd; i++) {
        if (Clay_PointerOver(CLAY_IDI("Item", i))) { newHover = i; break; }
    }
    if (newHover != hoverIdx) {
        hoverIdx = newHover;
        if (newHover >= 0) selectedIdx = newHover;
    }

    int itemW = cfg_iconSize + 24;
    int gridW = pageCols * itemW + (pageCols - 1) * cfg_hGap;

    bool hasPrev = !inFolder && currentPage > 0;
    bool hasNext = !inFolder && currentPage < totalPages - 1;

    Clay_BeginLayout();

    /* Full-screen background, children centered both axes */
    CLAY({
        .id = CLAY_ID("Outer"),
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = COL_BG
    }) {
        /* Folder header: back button + folder name — only shown when inside a folder */
        if (inFolder) {
            CLAY({
                .id = CLAY_ID("FolderHeader"),
                .layout = {
                    .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childGap = 14,
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                    .padding = { 0, 0, 0, 18 }
                }
            }) {
                CLAY({
                    .id = CLAY_ID("BackBtn"),
                    .layout = {
                        .sizing = { CLAY_SIZING_FIXED(ARROW_W), CLAY_SIZING_FIXED(38) },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                    },
                    .backgroundColor = COL_ARROW,
                    .cornerRadius = CLAY_CORNER_RADIUS(8)
                }) {
                    Clay_String s = { .chars = "<", .length = 1 };
                    CLAY_TEXT(s, CLAY_TEXT_CONFIG({
                        .fontId = 0, .fontSize = 22, .textColor = COL_TEXT
                    }));
                }
                {
                    UIFolder *uf = &uiFolders[currentFolderIdx];
                    Clay_String n = { .chars = uf->name, .length = (int32_t)strlen(uf->name) };
                    CLAY_TEXT(n, CLAY_TEXT_CONFIG({
                        .fontId = 0, .fontSize = (uint16_t)(cfg_fontSize + 4),
                        .textColor = COL_TEXT, .wrapMode = CLAY_TEXT_WRAP_NONE
                    }));
                }
            }
        }

        /* Row: [prev arrow] [grid] [next arrow] */
        CLAY({
            .id = CLAY_ID("PageRow"),
            .layout = {
                .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) },
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                .childGap = 20,
                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
            }
        }) {

            /* Prev arrow — invisible when not applicable */
            CLAY({
                .id = CLAY_ID("PrevPage"),
                .layout = {
                    .sizing = { CLAY_SIZING_FIXED(ARROW_W), CLAY_SIZING_GROW(0) },
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                },
                .backgroundColor = hasPrev ? COL_ARROW : COL_INVISIBLE,
                .cornerRadius = CLAY_CORNER_RADIUS(10)
            }) {
                if (hasPrev) {
                    Clay_String s = { .chars = "<", .length = 1 };
                    CLAY_TEXT(s, CLAY_TEXT_CONFIG({
                        .fontId = 0, .fontSize = 28, .textColor = COL_TEXT
                    }));
                }
            }

            /* Grid */
            CLAY({
                .id = CLAY_ID("Grid"),
                .layout = {
                    .sizing = { CLAY_SIZING_FIXED(gridW), CLAY_SIZING_FIT(0) },
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .childGap = cfg_vGap
                }
            }) {
                for (int row = 0; row < pageRows; row++) {
                    CLAY({
                        .id = CLAY_IDI("Row", row),
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                            .layoutDirection = CLAY_LEFT_TO_RIGHT,
                            .childGap = cfg_hGap
                        }
                    }) {
                        for (int col = 0; col < pageCols; col++) {
                            int idx = pageStart + row * pageCols + col;

                            /* Empty spacer keeps the last partial row aligned */
                            if (idx >= pageEnd) {
                                CLAY({
                                    .id = CLAY_IDI("Spacer", idx),
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIXED(itemW), CLAY_SIZING_FIXED(0) }
                                    }
                                }) {}
                                continue;
                            }

                            AppEntry *app = &aApps[idx];

                            /* Blank slot — invisible placeholder, same dimensions as a real item */
                            if (app->blank) {
                                CLAY({
                                    .id = CLAY_IDI("Item", idx),
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIXED(itemW), CLAY_SIZING_FIT(0) },
                                        .padding = { 8, 8, 8, 8 },
                                        .childGap = 6,
                                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER },
                                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                                    },
                                    .backgroundColor = (idx == selectedIdx) ? COL_ITEM : COL_INVISIBLE,
                                    .cornerRadius = CLAY_CORNER_RADIUS(8)
                                }) {
                                    CLAY({ .id = CLAY_IDI("BlankIcon", idx),
                                           .layout = { .sizing = { CLAY_SIZING_FIXED(cfg_iconSize),
                                                                    CLAY_SIZING_FIXED(cfg_iconSize) }}
                                    }) {}
                                    Clay_String sp = { .length = 1, .chars = " " };
                                    CLAY_TEXT(sp, CLAY_TEXT_CONFIG({
                                        .fontId = 0, .fontSize = (uint16_t)cfg_fontSize,
                                        .textColor = COL_INVISIBLE
                                    }));
                                }
                                continue;
                            }

                            CLAY({
                                .id = CLAY_IDI("Item", idx),
                                .layout = {
                                    .sizing = { CLAY_SIZING_FIXED(itemW), CLAY_SIZING_FIT(0) },
                                    .padding = { 8, 8, 8, 8 },
                                    .childGap = 6,
                                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER },
                                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                                },
                                .backgroundColor = (idx == selectedIdx) ? COL_ITEM : COL_INVISIBLE,
                                .cornerRadius = CLAY_CORNER_RADIUS(8)
                            }) {
                                if (app->icon) {
                                    CLAY({
                                        .id = CLAY_IDI("Icon", idx),
                                        .layout = {
                                            .sizing = {
                                                CLAY_SIZING_FIXED(cfg_iconSize),
                                                CLAY_SIZING_FIXED(cfg_iconSize)
                                            }
                                        },
                                        .image = { .imageData = app->icon }
                                    }) {}
                                } else if (app->iconFailed && errorIcon) {
                                    CLAY({
                                        .id = CLAY_IDI("Icon", idx),
                                        .layout = {
                                            .sizing = {
                                                CLAY_SIZING_FIXED(cfg_iconSize),
                                                CLAY_SIZING_FIXED(cfg_iconSize)
                                            }
                                        },
                                        .image = { .imageData = errorIcon }
                                    }) {}
                                } else {
                                    CLAY({
                                        .id = CLAY_IDI("NoIcon", idx),
                                        .layout = {
                                            .sizing = {
                                                CLAY_SIZING_FIXED(cfg_iconSize),
                                                CLAY_SIZING_FIXED(cfg_iconSize)
                                            }
                                        },
                                        .backgroundColor = COL_NO_ICON,
                                        .cornerRadius = CLAY_CORNER_RADIUS(6)
                                    }) {}
                                }

                                Clay_String name = {
                                    .length = (int32_t)strlen(app->name),
                                    .chars  = app->name
                                };
                                CLAY_TEXT(name, CLAY_TEXT_CONFIG({
                                    .fontId    = 0,
                                    .fontSize  = (uint16_t)cfg_fontSize,
                                    .textColor = COL_TEXT,
                                    .wrapMode  = CLAY_TEXT_WRAP_NONE
                                }));
                            }
                        }
                    }
                }
            }

            /* Next arrow */
            CLAY({
                .id = CLAY_ID("NextPage"),
                .layout = {
                    .sizing = { CLAY_SIZING_FIXED(ARROW_W), CLAY_SIZING_GROW(0) },
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                },
                .backgroundColor = hasNext ? COL_ARROW : COL_INVISIBLE,
                .cornerRadius = CLAY_CORNER_RADIUS(10)
            }) {
                if (hasNext) {
                    Clay_String s = { .chars = ">", .length = 1 };
                    CLAY_TEXT(s, CLAY_TEXT_CONFIG({
                        .fontId = 0, .fontSize = 28, .textColor = COL_TEXT
                    }));
                }
            }
        }
    }

    return Clay_EndLayout();
}

void ui_free(void) {
    for (int i = 0; i < appCount; i++) {
        if (apps[i].icon && !apps[i].isFolder) SDL_DestroyTexture(apps[i].icon);
    }
    for (int f = 0; f < uiFolderCount; f++) {
        if (uiFolders[f].icon) SDL_DestroyTexture(uiFolders[f].icon);
        free(uiFolders[f].apps);
    }
    appCount = pageInfoCount = uiFolderCount = 0;
    currentFolderIdx = -1;
    if (errorIcon) { SDL_DestroyTexture(errorIcon); errorIcon = NULL; }
}
