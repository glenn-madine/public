/*
 *  MECHO.C  -  "Modified ECHO"
 *
 *  Works like the DOS ECHO command, but lets you display the text
 *  in a chosen foreground and/or background color.
 *
 *  Usage:
 *      MECHO [/FG<ColorName>] [/BG<ColorName>] text [more text ...]
 *      MECHO /?
 *
 *  The /FG and /BG switches may appear in either order, and
 *  either (or both, or neither) may be used.
 *
 *  Examples:
 *      MECHO Hello there
 *      MECHO /FGRed "The message is displayed in red"
 *      MECHO /FGRed /BGGreen "Red text on a green background"
 *      MECHO /BGBlue /FGYellow Warning: disk almost full
 *
 *  Compile with Turbo C 2.01:
 *      TCC MECHO.C
 *  or from the IDE:  Project -> New Project -> MECHO.C -> Compile/Make
 */
 
#define MECHO_VERSION 1.22

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <ctype.h>

typedef struct {
    char *name;
    int   value;
} ColorEntry;

/* Standard Borland/Turbo C text colors (defined in conio.h, listed
   here again for clarity: BLACK..WHITE, values 0-15)              */
static ColorEntry fgColorTable[] = {
    { "Black",        BLACK        },
    { "Blue",         BLUE         },
    { "Green",        GREEN        },
    { "Cyan",         CYAN         },
    { "Red",          RED          },
    { "Magenta",      MAGENTA      },
    { "Brown",        BROWN        },
    { "LightGray",    LIGHTGRAY    },
    { "DarkGray",     DARKGRAY     },
    { "LightBlue",    LIGHTBLUE    },
    { "LightGreen",   LIGHTGREEN   },
    { "LightCyan",    LIGHTCYAN    },
    { "LightRed",     LIGHTRED     },
    { "LightMagenta", LIGHTMAGENTA },
    { "Yellow",       YELLOW       },
    { "White",        WHITE        }
};

#define NUM_FG_COLORS  (sizeof(fgColorTable) / sizeof(ColorEntry))

/* Text-mode backgrounds only support the 8 base colors (Black
   through LightGray) - the high-intensity bit is used for
   blinking instead, so Light* / White / Yellow etc. aren't valid
   background colors on standard DOS text-mode hardware.          */
static ColorEntry bgColorTable[] = {
    { "Black",     BLACK     },
    { "Blue",      BLUE      },
    { "Green",     GREEN     },
    { "Cyan",      CYAN      },
    { "Red",       RED       },
    { "Magenta",   MAGENTA   },
    { "Brown",     BROWN     },
    { "LightGray", LIGHTGRAY }
};

#define NUM_BG_COLORS (sizeof(bgColorTable) / sizeof(ColorEntry))
#define CMD_BUFSIZE   256

/* Look up a foreground color name (case-insensitive). Returns the
   Turbo C color value, or -1 if the name isn't recognized.        */
int FindColor(char *name)
{
    int i;

    for (i = 0; i < (int)NUM_FG_COLORS; i++) {
        if (stricmp(fgColorTable[i].name, name) == 0)
            return fgColorTable[i].value;
    }
    return -1;
}

/* Look up a background color name (case-insensitive). Returns the
   Turbo C color value, or -1 if the name isn't recognized.        */
int FindBGColor(char *name)
{
    int i;

    for (i = 0; i < (int)NUM_BG_COLORS; i++) {
        if (stricmp(bgColorTable[i].name, name) == 0)
            return bgColorTable[i].value;
    }
    return -1;
}

void ShowUsage(void)
{
    int i;

    printf("MECHO v%.2f - display text on screen, optionally in color\r\n\r\n", MECHO_VERSION);
    printf("Usage:  MECHO [/FG<ColorName>] [/BG<ColorName>] text [more text ...]\r\n");
    printf("        MECHO /?\r\n\r\n");
    printf("Example: MECHO /FGRed /BGGreen \"Red text on green background\"\r\n\r\n");
    printf("Available foreground colors:\r\n");
    for (i = 0; i < (int)NUM_FG_COLORS; i++)
        printf("   %s\r\n", fgColorTable[i].name);
    printf("\r\nAvailable background colors (no blink bit on text-mode hardware):\r\n");
    for (i = 0; i < (int)NUM_BG_COLORS; i++)
        printf("   %s\r\n", bgColorTable[i].name);
}

int main(int argc, char *argv[])
{
    int  startArg = 1;
    int  fgColor  = -1;
    int  bgColor  = -1;
    char buffer[CMD_BUFSIZE];
    int  i;

    if (argc < 2) {
        printf("\r\n");           /* behave like bare ECHO: blank line */
        return 0;
    }

    if (stricmp(argv[1], "/?") == 0) {
        ShowUsage();
        return 0;
    }

    /* Custom /FG<Name> and /BG<Name> switches, in
       whichever order they appear (either one is optional).
       e.g. /FGRed, /BGGreen, /FGLightGreen           */
    while (startArg < argc) {
        if (strnicmp(argv[startArg], "/BG", 3) == 0) {
            bgColor = FindBGColor(argv[startArg] + 3);
            if (bgColor == -1) {
                printf("MECHO: unknown background color '%s'\r\n", argv[startArg] + 3);
                printf("Type MECHO /? for a list of valid background colors.\r\n");
                return 1;
            }
            startArg++;
        } else if (strnicmp(argv[startArg], "/FG", 3) == 0) {
            fgColor = FindColor(argv[startArg] + 3);
            if (fgColor == -1) {
                printf("MECHO: unknown color '%s'\r\n", argv[startArg] + 3);
                printf("Type MECHO /? for a list of valid colors.\r\n");
                return 1;
            }
            startArg++;
        } else {
            break;
        }
    }

    if (startArg >= argc) {
        printf("\r\n");
        return 0;
    }

    /* Rebuild the message, ECHO-style, from the remaining args */
    buffer[0] = '\0';
    for (i = startArg; i < argc; i++) {
        if (i > startArg)
            strcat(buffer, " ");
        if (strlen(buffer) + strlen(argv[i]) < CMD_BUFSIZE - 1)
            strcat(buffer, argv[i]);
    }

    if (fgColor != -1 || bgColor != -1) {
        textcolor(fgColor != -1 ? fgColor : LIGHTGRAY);
        textbackground(bgColor != -1 ? bgColor : BLACK);
        cprintf("%s", buffer);
        textcolor(LIGHTGRAY);       /* restore normal colors      */
        textbackground(BLACK);
        cprintf("\r\n");
    } else {
        printf("%s\r\n", buffer);
    }

    return 0;
}