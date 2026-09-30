/* xlcons.c - Windows console cursor support for the line editor */
/*
 * This file is separate from xl1.c because <windows.h> defines names
 * (CONTEXT, CHAR, ...) that clash with XLISP's own definitions.
 */

#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* oscurback - move the console cursor n cells to the left, following
   the text back onto the previous row when a long line has wrapped.
   Returns nonzero on success, 0 if the console could not be queried
   (the caller then falls back to backspace characters). */
int oscurback(int n)
{
    CONSOLE_SCREEN_BUFFER_INFO bi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    long width, pos;
    COORD c;

    if (n <= 0)
	return (1);
    if (h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h,&bi))
	return (0);
    width = (long)bi.dwSize.X;
    pos = (long)bi.dwCursorPosition.Y * width + (long)bi.dwCursorPosition.X - n;
    if (pos < 0)
	pos = 0;
    c.X = (SHORT)(pos % width);
    c.Y = (SHORT)(pos / width);
    return (SetConsoleCursorPosition(h,c) != 0);
}

#else

/* oscurback - not available: the caller uses backspace characters */
int oscurback(int n)
{
    (void)n;
    return (0);
}

#endif
