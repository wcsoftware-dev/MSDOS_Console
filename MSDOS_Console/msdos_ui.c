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
} FileItem;

static HANDLE hConsole;

// Console color helpers
enum {
    ATTR_BG_BLUE = BACKGROUND_BLUE,
    ATTR_WHITE_ON_BLUE = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | ATTR_BG_BLUE,
    ATTR_YELLOW_ON_BLUE = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY | ATTR_BG_BLUE,
    ATTR_STATUS = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | ATTR_BG_BLUE
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
        if (*count >= MAX_ITEMS) break;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
}

static void draw_ui(const char* cwd, FileItem* items, int count, int sel) {
    COORD size = get_console_size();
    int w = size.X, h = size.Y;
    int list_h = h - 2; // leave bottom line for status
    // Clear and set background
    for (int y = 0; y < h; ++y) fill_line(y, ATTR_WHITE_ON_BLUE, w);

    // Title
    char title[512];
    snprintf(title, sizeof(title), " MS-DOS Style File Manager - %s ", cwd);
    put_text(0, 0, title, ATTR_WHITE_ON_BLUE);

    // File list
    for (int i = 0; i < list_h - 1 && i < count; ++i) {
        WORD attr = (i == sel) ? ATTR_YELLOW_ON_BLUE : ATTR_WHITE_ON_BLUE;
        char line[1024];
        snprintf(line, sizeof(line), " %c %s", items[i].is_dir ? 'D' : ' ', items[i].name);
        // Truncate to width
        if ((int)strlen(line) > w) line[w] = 0;
        put_text(0, i + 1, line, attr);
    }

    // Status bar at bottom
    char status[1024];
    snprintf(status, sizeof(status), " Enter: open   Backspace: up   Q: quit    Selected: %s ", (count>0?items[sel].name:""));
    int status_y = h - 1;
    // Fill then print status text
    fill_line(status_y, ATTR_STATUS, w);
    put_text(0, status_y, status, ATTR_STATUS);
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
            if (ch2 == 72) { // up
                if (sel > 0) sel--;
            } else if (ch2 == 80) { // down
                if (sel < count - 1) sel++;
            }
        } else {
            if (ch == 13) { // Enter
                if (count > 0 && items[sel].is_dir) {
                    char newpath[MAX_PATH];
                    if (strcmp(items[sel].name, "..") == 0) {
                        SetCurrentDirectoryA("..");
                    } else {
                        snprintf(newpath, sizeof(newpath), "%s\\%s", cwd, items[sel].name);
                        SetCurrentDirectoryA(newpath);
                    }
                    GetCurrentDirectoryA(MAX_PATH, cwd);
                    load_directory(cwd, items, &count);
                    sel = 0;
                }
            } else if (ch == 8) { // Backspace
                SetCurrentDirectoryA("..");
                GetCurrentDirectoryA(MAX_PATH, cwd);
                load_directory(cwd, items, &count);
                sel = 0;
            } else if (ch == 'q' || ch == 'Q') {
                running = 0;
            }
        }
        draw_ui(cwd, items, count, sel);
    }

    // Restore cursor before exit
    ci.bVisible = TRUE;
    SetConsoleCursorInfo(hConsole, &ci);
    // Reset attributes
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    return 0;
}