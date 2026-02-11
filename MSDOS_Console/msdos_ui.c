// msdos_ui.c - Minimal MS-DOS style terminal file manager (Windows console, C)
// Compile in Visual Studio as a C file (set /TC) or use: cl /W4 /TC msdos_ui.c

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>

#define MAX_ITEMS 1024
#define MAX_NAME  260

typedef struct {
    char name[MAX_NAME];
    BOOL is_dir;
    unsigned long long size;
    SYSTEMTIME mtime;
} FileItem;

static HANDLE hConsole;

// Pane focus and selections
typedef enum { PANE_DIR = 0, PANE_FILES = 1, PANE_MAIN = 2, PANE_TASKS = 3 } Pane;
static Pane cur_pane = PANE_DIR;
static int dir_sel = 0;
static int file_sel = 0;
static int main_sel = 0;
static int task_sel = 0;
static int dir_offset = 0;
static int file_offset = 0;

// Console color helpers
enum {
    ATTR_BG_BLUE = BACKGROUND_BLUE,
    ATTR_WHITE_ON_BLUE = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | ATTR_BG_BLUE,
    ATTR_YELLOW_ON_BLUE = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY | ATTR_BG_BLUE,
    /* Bright green track on blue background for scrollbars */
    ATTR_SCROLL = FOREGROUND_GREEN | FOREGROUND_INTENSITY | ATTR_BG_BLUE,
    ATTR_STATUS = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | ATTR_BG_BLUE,
    /* Black text on bright yellow background for selection (classic DOS highlight) */
    ATTR_HILITE = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_INTENSITY
};

static COORD get_console_size() {
    CONSOLE_SCREEN_BUFFER_INFO sbi;
    GetConsoleScreenBufferInfo(hConsole, &sbi);
    COORD c = { (SHORT)(sbi.srWindow.Right - sbi.srWindow.Left + 1),
                (SHORT)(sbi.srWindow.Bottom - sbi.srWindow.Top + 1) };
    return c;
}

static void put_text(int x, int y, const char* text, WORD attr) {
    SetConsoleCursorPosition(hConsole, (COORD){ (SHORT)x, (SHORT)y });
    SetConsoleTextAttribute(hConsole, attr);
    DWORD written;
    WriteConsoleA(hConsole, text, (DWORD)strlen(text), &written, NULL);
}

static void fill_line(int y, WORD attr, int width) {
    char *buf = (char*)malloc(width + 1);
    if (!buf) return;
    memset(buf, ' ', width);
    buf[width] = 0;
    put_text(0, y, buf, attr);
    free(buf);
}

static void load_directory(const char* path, FileItem* items, int* count) {
    char search[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE hFind;

    snprintf(search, sizeof(search), "%s\\*", path);
    *count = 0;
    hFind = FindFirstFileA(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(fd.cFileName, ".") == 0) continue;
        FileItem *it = &items[(*count)++];
        strncpy_s(it->name, MAX_NAME, fd.cFileName, MAX_NAME-1);
        it->name[MAX_NAME-1] = 0;
        it->is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        it->size = 0;
        ZeroMemory(&it->mtime, sizeof(it->mtime));

        /* Get size and last write time */
        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s\\%s", path, fd.cFileName);
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExA(full, GetFileExInfoStandard, &fad)) {
            it->size = ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
            FILETIME localFt;
            FileTimeToLocalFileTime(&fad.ftLastWriteTime, &localFt);
            FileTimeToSystemTime(&localFt, &it->mtime);
        }

        if (*count >= MAX_ITEMS) break;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
}

static void draw_ui(const char* cwd, FileItem* items, int count, int sel) {
    COORD size = get_console_size();
    int w = size.X, h = size.Y;
    int total = w * h;
    CHAR_INFO *buf = (CHAR_INFO*)malloc(sizeof(CHAR_INFO) * total);
    if (!buf) return;

    // fill background
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            buf[idx].Char.AsciiChar = ' ';
            buf[idx].Attributes = ATTR_WHITE_ON_BLUE;
        }
    }

#define BUF_PUT_TEXT(px,py,txt,attr) do { \
        const char *_s = (txt); \
        for (int _i = 0; _i < (int)strlen(_s); ++_i) { \
            int _x = (px) + _i; int _y = (py); \
            if (_x < 0 || _x >= w || _y < 0 || _y >= h) continue; \
            int _idx = _y * w + _x; \
            buf[_idx].Char.AsciiChar = _s[_i]; \
            buf[_idx].Attributes = (attr); \
        } \
    } while (0)

    int content_top = 3;
    int content_bottom = h - 2;
    int content_h = content_bottom - content_top + 1;
    int left_w = w / 3;
    int mid_x = left_w;
    int top_h = content_h / 2;
    int mid_y = content_top + top_h;
    int bottom_h = content_h - top_h - 1; // lines available for bottom panes

    // title/menu/path
    char title[256]; snprintf(title, sizeof(title), " MS-DOS Shell ");
    int title_x = (w > (int)strlen(title)) ? (w/2 - (int)strlen(title)/2) : 0;
    BUF_PUT_TEXT(title_x, 0, title, ATTR_WHITE_ON_BLUE);
    BUF_PUT_TEXT(0, 1, " File  Options  View  Help", ATTR_WHITE_ON_BLUE);
    char pathbar[1024]; snprintf(pathbar, sizeof(pathbar), " %s", cwd); BUF_PUT_TEXT(0, 2, pathbar, ATTR_WHITE_ON_BLUE);

    // dividers
    for (int y = content_top; y <= content_bottom; ++y) BUF_PUT_TEXT(mid_x, y, "|", ATTR_WHITE_ON_BLUE);
    for (int x = 0; x < w; ++x) BUF_PUT_TEXT(x, mid_y, "-", ATTR_WHITE_ON_BLUE);

    // Build lists
    int dir_idx[MAX_ITEMS]; int file_idx[MAX_ITEMS]; int dcount = 0, fcount = 0;
    for (int i = 0; i < count; ++i) { if (items[i].is_dir) dir_idx[dcount++] = i; else file_idx[fcount++] = i; }

    if (dcount == 0) dir_sel = 0; else if (dir_sel >= dcount) dir_sel = dcount - 1;
    if (fcount == 0) file_sel = 0; else if (file_sel >= fcount) file_sel = fcount - 1;

    // directory header and count
    BUF_PUT_TEXT(1, content_top, "Directory Tree", ATTR_WHITE_ON_BLUE);
    char cntbuf[32]; int selpos = (dcount>0)?(dir_sel+1):0; snprintf(cntbuf,sizeof(cntbuf),"%d/%d",selpos,dcount);
    int posx = mid_x - (int)strlen(cntbuf) - 1; if (posx < 0) posx = 0; BUF_PUT_TEXT(posx, content_top, cntbuf, ATTR_WHITE_ON_BLUE);

    int dt_y = content_top + 1; int dt_max = (mid_y - 1) - dt_y + 1; int visible_dirs = dt_max; if (visible_dirs < 0) visible_dirs = 0;
    if (dir_offset < 0) dir_offset = 0; if (dir_offset > dcount - visible_dirs) dir_offset = dcount - visible_dirs; if (dir_offset < 0) dir_offset = 0;
    for (int i = 0; i < visible_dirs && (i + dir_offset) < dcount; ++i) {
        int idx = dir_idx[i + dir_offset]; WORD attr = (cur_pane == PANE_DIR && (i + dir_offset) == dir_sel) ? ATTR_HILITE : ATTR_WHITE_ON_BLUE;
        char line[512]; snprintf(line,sizeof(line),"  [%c] %s", 'D', items[idx].name); if ((int)strlen(line) > left_w-2) line[left_w-2] = '\0'; BUF_PUT_TEXT(1, dt_y + i, line, attr);
    }

    // left scrollbar
    if (dcount > visible_dirs && visible_dirs > 0) {
        int col = mid_x - 1; for (int y = dt_y; y < dt_y + visible_dirs; ++y) BUF_PUT_TEXT(col, y, "|", ATTR_SCROLL);
        int thumb_pos = dt_y; if (dcount > 1) thumb_pos = dt_y + (dir_offset * (visible_dirs - 1)) / (dcount - 1);
        if (thumb_pos < dt_y) thumb_pos = dt_y; if (thumb_pos > dt_y + visible_dirs - 1) thumb_pos = dt_y + visible_dirs - 1; BUF_PUT_TEXT(col, thumb_pos, "O", ATTR_HILITE);
    }

    // files header and list
    BUF_PUT_TEXT(mid_x+2, content_top, "Files", ATTR_WHITE_ON_BLUE);
    selpos = (fcount>0)?(file_sel+1):0; snprintf(cntbuf,sizeof(cntbuf),"%d/%d",selpos,fcount); posx = w - (int)strlen(cntbuf) - 1; if (posx < mid_x+2) posx = mid_x+2; BUF_PUT_TEXT(posx, content_top, cntbuf, ATTR_WHITE_ON_BLUE);
    int fl_y = content_top + 1; int fl_max = (mid_y - 1) - fl_y + 1; int visible_files = fl_max; if (visible_files < 0) visible_files = 0;
    if (file_offset < 0) file_offset = 0; if (file_offset > fcount - visible_files) file_offset = fcount - visible_files; if (file_offset < 0) file_offset = 0;
    for (int i = 0; i < visible_files && (i + file_offset) < fcount; ++i) {
        int idx = file_idx[i + file_offset]; FileItem *it = &items[idx]; WORD attr = (cur_pane == PANE_FILES && (i + file_offset) == file_sel) ? ATTR_HILITE : ATTR_WHITE_ON_BLUE;
        char line[1024]; char dt[64] = ""; if (it->mtime.wYear != 0) { int hour = it->mtime.wHour; int hour12 = hour % 12; if (hour12 == 0) hour12 = 12; const char *ampm = (hour >= 12) ? "PM" : "AM"; snprintf(dt, sizeof(dt), "%02d/%02d/%04d %02d:%02d %s", it->mtime.wMonth, it->mtime.wDay, it->mtime.wYear, hour12, it->mtime.wMinute, ampm); }
        char sizebuf[32] = ""; if (!it->is_dir) snprintf(sizebuf, sizeof(sizebuf), "%10llu", it->size); snprintf(line, sizeof(line), "%s %s %s", dt, sizebuf, it->name);
        int available = w - (mid_x + 3); if ((int)strlen(line) > available) line[available] = '\0'; BUF_PUT_TEXT(mid_x+2, fl_y + i, line, attr);
    }

    // right scrollbar
    if (fcount > visible_files && visible_files > 0) {
        int col = w - 1; for (int y = fl_y; y < fl_y + visible_files; ++y) BUF_PUT_TEXT(col, y, "|", ATTR_SCROLL);
        int thumb_pos = fl_y; if (fcount > 1) thumb_pos = fl_y + (file_offset * (visible_files - 1)) / (fcount - 1);
        if (thumb_pos < fl_y) thumb_pos = fl_y; if (thumb_pos > fl_y + visible_files - 1) thumb_pos = fl_y + visible_files - 1; BUF_PUT_TEXT(col, thumb_pos, "O", ATTR_HILITE);
    }

    // bottom panes
    BUF_PUT_TEXT(1, mid_y+1, "Main", ATTR_WHITE_ON_BLUE);
    const char *main_items[] = { "Command Prompt", "Editor", "MS-DOS QBasic", "Disk Utilities" };
    int main_count = sizeof(main_items)/sizeof(main_items[0]);
    for (int i = 0; i < bottom_h && i < main_count; ++i) { WORD attr = (cur_pane == PANE_MAIN && i == main_sel) ? ATTR_HILITE : ATTR_WHITE_ON_BLUE; BUF_PUT_TEXT(1, mid_y+2 + i, main_items[i], attr); }
    BUF_PUT_TEXT(mid_x+2, mid_y+1, "Active Task List", ATTR_WHITE_ON_BLUE);
    const char *tasks[] = { "Command Prompt" };
    int tcount = 1;
    for (int i = 0; i < bottom_h && i < tcount; ++i) { WORD attr = (cur_pane == PANE_TASKS && i == task_sel) ? ATTR_HILITE : ATTR_WHITE_ON_BLUE; BUF_PUT_TEXT(mid_x+2, mid_y+2 + i, tasks[i], attr); }

    // status bar
    char status[1024];
    /* determine selected name according to focused pane */
    char selected[512] = "";
    if (cur_pane == PANE_DIR) {
        if (dcount > 0 && dir_sel >= 0 && dir_sel < dcount) {
            int sel_idx = dir_idx[dir_sel];
            strncpy_s(selected, sizeof(selected), items[sel_idx].name, _TRUNCATE);
        }
    } else if (cur_pane == PANE_FILES) {
        if (fcount > 0 && file_sel >= 0 && file_sel < fcount) {
            int sel_idx = file_idx[file_sel];
            strncpy_s(selected, sizeof(selected), items[sel_idx].name, _TRUNCATE);
        }
    } else if (cur_pane == PANE_MAIN) {
        const char *main_items[] = { "Command Prompt", "Editor", "MS-DOS QBasic", "Disk Utilities" };
        int main_count = sizeof(main_items)/sizeof(main_items[0]);
        if (main_sel >= 0 && main_sel < main_count) strncpy_s(selected, sizeof(selected), main_items[main_sel], _TRUNCATE);
    } else if (cur_pane == PANE_TASKS) {
        const char *tasks[] = { "Command Prompt" };
        int tcount = 1;
        if (task_sel >= 0 && task_sel < tcount) strncpy_s(selected, sizeof(selected), tasks[task_sel], _TRUNCATE);
    }
    snprintf(status, sizeof(status), " Enter: open   Backspace: up   PgUp/PgDn: page   Home/End: top/bottom   Q: quit    Selected: %s ", (selected[0]?selected:"") );
    int status_y = h - 1; for (int x = 0; x < w; ++x) { int idx = status_y * w + x; buf[idx].Char.AsciiChar = ' '; buf[idx].Attributes = ATTR_STATUS; }
    BUF_PUT_TEXT(0, status_y, status, ATTR_STATUS);

    // write buffer to console
    COORD bufSize = { (SHORT)w, (SHORT)h };
    COORD bufCoord = { 0, 0 };
    SMALL_RECT writeRect = { 0, 0, (SHORT)(w - 1), (SHORT)(h - 1) };
    WriteConsoleOutputA(hConsole, buf, bufSize, bufCoord, &writeRect);

    free(buf);
    (void)sel; /* avoid unused param warning */
}

int main(void) {
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) return 1;

    // Ensure console cursor invisible
    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(hConsole, &ci);
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &ci);

    // Set initial attributes (blue background)
    SetConsoleTextAttribute(hConsole, ATTR_WHITE_ON_BLUE);

    char cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, cwd);

    FileItem items[MAX_ITEMS];
    int count = 0;
    int sel = 0;
    load_directory(cwd, items, &count);

    draw_ui(cwd, items, count, sel);

    int running = 1;
    while (running) {
        int ch = _getch();
        if (ch == 0 || ch == 0xE0) {
            int ch2 = _getch();
            /* compute visible rows for top panes */
            COORD size = get_console_size();
            int w = size.X, h = size.Y;
            int content_top = 3;
            int content_bottom = h - 2;
            int content_h = content_bottom - content_top + 1;
            int top_h = content_h / 2;
            int visible_lines = top_h - 1; if (visible_lines < 0) visible_lines = 0;

            /* count dirs/files */
            int dcount = 0, fcount = 0;
            for (int i = 0; i < count; ++i) { if (items[i].is_dir) dcount++; else fcount++; }

            if (ch2 == 72) { // up
                if (cur_pane == PANE_DIR) {
                    if (dir_sel > 0) dir_sel--;
                    if (dir_sel < dir_offset) dir_offset = dir_sel;
                    if (dir_sel < 0) dir_sel = 0;
                } else if (cur_pane == PANE_FILES) {
                    if (file_sel > 0) file_sel--;
                    if (file_sel < file_offset) file_offset = file_sel;
                    if (file_sel < 0) file_sel = 0;
                } else if (cur_pane == PANE_MAIN) { if (main_sel > 0) main_sel--; }
                else if (cur_pane == PANE_TASKS) { if (task_sel > 0) task_sel--; }
            } else if (ch2 == 80) { // down
                if (cur_pane == PANE_DIR) {
                    if (dir_sel < dcount - 1) dir_sel++;
                    if (dir_sel >= dir_offset + visible_lines) dir_offset = dir_sel - visible_lines + 1;
                } else if (cur_pane == PANE_FILES) {
                    if (file_sel < fcount - 1) file_sel++;
                    if (file_sel >= file_offset + visible_lines) file_offset = file_sel - visible_lines + 1;
                } else if (cur_pane == PANE_MAIN) { if (main_sel < MAX_ITEMS-1) main_sel++; }
                else if (cur_pane == PANE_TASKS) { if (task_sel < MAX_ITEMS-1) task_sel++; }
            } else if (ch2 == 73) { // PageUp
                if (visible_lines <= 0) continue;
                if (cur_pane == PANE_DIR) {
                    dir_sel -= visible_lines; if (dir_sel < 0) dir_sel = 0;
                    if (dir_sel < dir_offset) dir_offset = dir_sel;
                } else if (cur_pane == PANE_FILES) {
                    file_sel -= visible_lines; if (file_sel < 0) file_sel = 0;
                    if (file_sel < file_offset) file_offset = file_sel;
                } else if (cur_pane == PANE_MAIN) { main_sel = 0; }
                else if (cur_pane == PANE_TASKS) { task_sel = 0; }
            } else if (ch2 == 81) { // PageDown
                if (visible_lines <= 0) continue;
                if (cur_pane == PANE_DIR) {
                    if (dir_sel + visible_lines < dcount) dir_sel += visible_lines; else dir_sel = dcount - 1;
                    if (dir_sel >= dir_offset + visible_lines) dir_offset = dir_sel - visible_lines + 1;
                } else if (cur_pane == PANE_FILES) {
                    if (file_sel + visible_lines < fcount) file_sel += visible_lines; else file_sel = fcount - 1;
                    if (file_sel >= file_offset + visible_lines) file_offset = file_sel - visible_lines + 1;
                } else if (cur_pane == PANE_MAIN) { main_sel = main_sel + visible_lines; }
                else if (cur_pane == PANE_TASKS) { task_sel = task_sel + visible_lines; }
            } else if (ch2 == 71) { // Home
                if (cur_pane == PANE_DIR) { dir_sel = 0; dir_offset = 0; }
                else if (cur_pane == PANE_FILES) { file_sel = 0; file_offset = 0; }
                else if (cur_pane == PANE_MAIN) { main_sel = 0; }
                else if (cur_pane == PANE_TASKS) { task_sel = 0; }
            } else if (ch2 == 79) { // End
                if (cur_pane == PANE_DIR) { dir_sel = (dcount>0)?(dcount-1):0; dir_offset = (dcount>visible_lines)?(dcount-visible_lines):0; }
                else if (cur_pane == PANE_FILES) { file_sel = (fcount>0)?(fcount-1):0; file_offset = (fcount>visible_lines)?(fcount-visible_lines):0; }
                else if (cur_pane == PANE_MAIN) { main_sel = /* last */ 3; }
                else if (cur_pane == PANE_TASKS) { task_sel = /* last */ 0; }
            }
        } else {
            if (ch == 9) { // Tab - switch pane (Shift+Tab to go backward)
                SHORT shiftState = GetAsyncKeyState(VK_SHIFT);
                if (shiftState & 0x8000) {
                    // Shift is down: move focus backward
                    cur_pane = (Pane)((cur_pane + 4 - 1) % 4);
                } else {
                    // Normal Tab: move focus forward
                    cur_pane = (Pane)((cur_pane + 1) % 4);
                }
            } else if (ch == 13) { // Enter
                // If directory pane focused, change directory to selected dir
                if (cur_pane == PANE_DIR) {
                    // build dirs list to map dir_sel to items
                    int dcount = 0;
                    int dir_idx[MAX_ITEMS];
                    for (int i = 0; i < count; ++i) if (items[i].is_dir) dir_idx[dcount++] = i;
                    if (dcount > 0 && dir_sel < dcount) {
                        const char *dname = items[dir_idx[dir_sel]].name;
                        if (strcmp(dname, "..") == 0) SetCurrentDirectoryA("..");
                        else {
                            char newpath[MAX_PATH];
                            snprintf(newpath, sizeof(newpath), "%s\\%s", cwd, dname);
                            SetCurrentDirectoryA(newpath);
                        }
                        GetCurrentDirectoryA(MAX_PATH, cwd);
                        load_directory(cwd, items, &count);
                        dir_sel = 0; file_sel = 0;
                    }
                } else if (cur_pane == PANE_FILES) {
                    // (placeholder) open file: do nothing for now
                }
            } else if (ch == 8) { // Backspace
                SetCurrentDirectoryA("..");
                GetCurrentDirectoryA(MAX_PATH, cwd);
                load_directory(cwd, items, &count);
                dir_sel = 0; file_sel = 0;
            } else if (ch == 'q' || ch == 'Q') {
                running = 0;
            }
        }
        draw_ui(cwd, items, count, 0);
    }

    // Restore cursor before exit
    ci.bVisible = TRUE;
    SetConsoleCursorInfo(hConsole, &ci);
    // Reset attributes
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    return 0;
}