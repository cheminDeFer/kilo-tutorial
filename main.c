/*** includes ***/
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#define CTRLKEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_DOWN ,
    ARROW_UP   ,
    ARROW_RIGHT,
};

/*** data ***/
struct editorConfig {
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};
struct editorConfig E;
/*** terminal ***/
void
die (const char *s)
{
    write (STDOUT_FILENO, "\x1b[2J", 4);
    write (STDOUT_FILENO, "\x1b[H", 3);
    perror (s);
    exit (1);
}
void
disableRawMode (void)
{
    if (tcsetattr (STDIN_FILENO, TCSAFLUSH,
                   &E.orig_termios) == -1 ) die ("tcsetattr");
}
void
enableRawMode (void)
{
    if (tcgetattr (STDIN_FILENO, &E.orig_termios) < 0 ) die ("tcgetattr");
    atexit (disableRawMode);
    struct termios raw;
    if ( tcgetattr (STDIN_FILENO, &raw)  < 0  ) die ("tcgetattr");
    raw.c_iflag &= ~ (BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_lflag &= ~ (OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~ (ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 2;
    if (tcsetattr (STDIN_FILENO, TCSAFLUSH, &raw) < 0 ) {
        fprintf (stderr, "Error: cannot set with tcsetattr due to '%s'\r\n",
                 strerror (errno));
        exit (1);
    }
}
/*** input ***/
int
editorReadKey (void)
{
    char c;
    int nread;
    while ((nread = read (STDIN_FILENO, &c, 1)) != 1 ) {
        if (nread == -1 && errno != EAGAIN) die ("read");
        switch (c) {
        case 'h':
            return ARROW_LEFT;
        case 'j':
            return ARROW_DOWN;
        case 'k':
            return ARROW_UP;
	case 'l':
             return ARROW_RIGHT;
        }
    }
    return (int)c;
}
int
getCursorPositon (int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;
    if (write (STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    while (i < sizeof (buf) - 1) {
        if (read (STDIN_FILENO, &buf[i], 1) != 1 ) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    printf ("\r\n");
    printf ("&buf[1]: '%s'\r\n", &buf[1]);
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf (&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
}
int
getWindowSize (int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 ) {
        if (write (STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPositon (rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
/*** append buffer ***/
struct ABUF {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void
abAppend (struct ABUF *ab, const char *s, int len)
{
    char *new_ab = realloc (ab->b, ab->len + len);
    if (new_ab == NULL) {
        die ("out of memory @ abAppend");
    }
    memcpy (&new_ab[ab->len], s, len);
    ab->b = new_ab;
    ab->len += len;
}
void
abFree (struct ABUF *ab)
{
    free (ab->b);
}

void
editorMoveCursor (int c)
{
    switch (c) {
    case ARROW_LEFT:
        if (E.cx != 0 )
            E.cx --;
        break;
    case ARROW_UP:
        if (E.cy != 0)
            E.cy --;
        break;
    case ARROW_DOWN:
        if (E.cy != E.screencols - 1)
            E.cy ++;
        break;
    case ARROW_RIGHT:
        if (E.cy != E.screenrows - 1)
            E.cx++;
        break;
    }
}
void
editorProcessKey (void)
{
    int c = editorReadKey();
    if (iscntrl (c)) {
        fprintf (stderr, "%d\r\n", c);
    } else {
        fprintf (stderr,"%d ('%c')\r\n", c, c);
    }
    switch (c) {
    case CTRLKEY ('q'):
        exit (0);
        break;
    case ARROW_LEFT:
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_RIGHT:
        editorMoveCursor (c);
    }
}
/*** output ***/
void
editorDrawRows (struct ABUF *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++) {
        if (y == E.screenrows / 3 ) {
            char welcome[80];
            int welcome_len = snprintf (welcome, sizeof (welcome),
                                        "KILO editor-- version %s", KILO_VERSION);
            if (welcome_len > E.screencols ) welcome_len = E.screencols;
            int padding = (E.screencols - welcome_len) / 2;
            if (padding) {
                abAppend (ab, "~", 1);
                padding--;
            }
            while (padding--) abAppend (ab, " ", 1);
            abAppend (ab, welcome, welcome_len) ;
        } else {
            abAppend (ab, "~", 1) ;
        }
        abAppend (ab, "\x1b[K", 3) ;
        if (y !=  E.screenrows - 1) {
            abAppend (ab, "\r\n", 2) ;
        }
    }
}
void
editorRefreshScreen (void)
{
    struct ABUF ab = ABUF_INIT;
    abAppend (&ab, "\x1b[?25l", 6);
    abAppend (&ab, "\x1b[H", 3);
    editorDrawRows (&ab);
    char cursor_buf[32];
    snprintf (cursor_buf, sizeof (cursor_buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    fprintf (stderr, "%s\n", cursor_buf);
    fprintf (stderr, "%zu\n", strlen (cursor_buf));
    abAppend (&ab, cursor_buf, strlen (cursor_buf));
    abAppend (&ab, "\x1b[?25h", 6);
    if ( write (STDOUT_FILENO, ab.b, ab.len ) != ab.len ) die ("cannot write ab");
    abFree (&ab);
}

/*** init ***/
void
initEditor()
{
    E.cx = 0;
    E.cy = 0;
    if (getWindowSize (&E.screenrows, &E.screencols) == -1 ) die ("getWindowSize");
}
int
main (int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    while (1) {
        editorRefreshScreen();
        editorProcessKey();
    }
    return 0;
}
