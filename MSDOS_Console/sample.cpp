//#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
//#include <ncurses.h>
//
//// Function to draw a DOS-style window with a title
//void draw_window(int starty, int startx, int height, int width, const char* title) {
//    WINDOW* win = newwin(height, width, starty, startx);
//    box(win, 0, 0); // Draw border
//    mvwprintw(win, 0, 2, "[ %s ]", title); // Title in top border
//    wrefresh(win);
//}
//
//int main() {
//    // Initialize ncurses
//    initscr();
//    noecho();            // Don't echo typed characters
//    cbreak();            // Disable line buffering
//    keypad(stdscr, TRUE); // Enable arrow keys
//    curs_set(0);         // Hide cursor
//
//    // Check if terminal supports colors
//    if (!has_colors()) {
//        endwin();
//        fprintf(stderr, "Your terminal does not support colors.\n");
//        return 1;
//    }
//
//    start_color();
//    // Define DOS-like colors (blue background, white text)
//    init_pair(1, COLOR_WHITE, COLOR_BLUE);
//    init_pair(2, COLOR_YELLOW, COLOR_BLUE);
//
//    // Set background color
//    bkgd(COLOR_PAIR(1));
//    clear();
//
//    // Draw a title bar
//    attron(COLOR_PAIR(2) | A_BOLD);
//    mvprintw(0, 2, "MS-DOS Style Terminal UI - Press 'q' to Quit");
//    attroff(COLOR_PAIR(2) | A_BOLD);
//
//    // Draw a sample window
//    draw_window(3, 5, 10, 40, "Main Menu");
//
//    // Menu items
//    const char* menu[] = { "Option 1", "Option 2", "Option 3", "Exit" };
//    int choice = 0;
//    int ch;
//    int menu_size = sizeof(menu) / sizeof(menu[0]);
//
//    // Menu loop
//    while (1) {
//        for (int i = 0; i < menu_size; i++) {
//            if (i == choice) {
//                attron(A_REVERSE);
//                mvprintw(5 + i, 7, "%s", menu[i]);
//                attroff(A_REVERSE);
//            }
//            else {
//                mvprintw(5 + i, 7, "%s", menu[i]);
//            }
//        }
//
//        ch = getch();
//        if (ch == 'q' || (choice == menu_size - 1 && ch == '\n')) {
//            break; // Quit
//        }
//        else if (ch == KEY_UP) {
//            choice = (choice - 1 + menu_size) % menu_size;
//        }
//        else if (ch == KEY_DOWN) {
//            choice = (choice + 1) % menu_size;
//        }
//    }
//
//    // End ncurses mode
//    endwin();
//    return 0;
//}