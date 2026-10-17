/*
 * wincon.c
 *
 * The Windows console: tells a pseudo console, whose screen a terminal
 * program draws, from a console window, and mends the console window. In an
 * alternate screen buffer the window only disables a scroll bar that has
 * nothing to scroll, and lays the screen out as if that bar were not there: a
 * bar that a resize shows for a moment stays, covering the last row or
 * columns of the screen. Such a bar is hidden.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#ifdef __CYGWIN__

#include <string.h>
#include <unistd.h>
#include <windows.h>

#include "host.h"

bool pseudo_console(void)
{
    const char *tty = ttyname(STDOUT_FILENO);
    HWND w = GetConsoleWindow();

    /* A pseudo console's window is hidden: a terminal draws its screen. */
    return (tty != NULL) && !strncmp(tty, "/dev/cons", 9) && (w != NULL)
        && !IsWindowVisible(w);
}

/* Hides scroll bar @bar, SB_HORZ or SB_VERT, of window @w if it is shown
 * with nothing to scroll. */
static void hide_idle_bar(HWND w, int bar)
{
    SCROLLINFO si = { .cbSize = sizeof(si), .fMask = SIF_RANGE | SIF_PAGE };
    LONG shown = GetWindowLongW(w, GWL_STYLE)
        & ((bar == SB_HORZ) ? WS_HSCROLL : WS_VSCROLL);

    if (shown && GetScrollInfo(w, bar, &si)
        && (si.nPage > (UINT)(si.nMax - si.nMin)))
        ShowScrollBar(w, bar, FALSE);
}

void check_console_bars(void)
{
    HWND w = GetConsoleWindow();
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO b;
    CONSOLE_FONT_INFO f;
    COORD cell;
    RECT r;

    /* A pseudo console's hidden window has nothing to mend. */
    if ((w == NULL) || !IsWindowVisible(w)
        || !GetConsoleScreenBufferInfo(out, &b)
        || !GetCurrentConsoleFont(out, FALSE, &f) || !GetClientRect(w, &r))
        return;
    /* To a program that is not DPI-aware, as this one, the window's client
     * area comes in the same logical pixels as the font's size. */
    cell = GetConsoleFontSize(out, f.nFont);
    if ((cell.X <= 0) || (cell.Y <= 0))
        return;
    if (r.bottom / cell.Y < b.srWindow.Bottom - b.srWindow.Top + 1)
        hide_idle_bar(w, SB_HORZ);
    if (r.right / cell.X < b.srWindow.Right - b.srWindow.Left + 1)
        hide_idle_bar(w, SB_VERT);
}

#endif /* __CYGWIN__ */
