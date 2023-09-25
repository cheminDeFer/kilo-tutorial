/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <time.h>
#include <stdarg.h>

/* Include the Lua API header files. */
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>


#define CTRLKEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_DOWN,
    ARROW_UP,
    ARROW_RIGHT
};

typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

/*** data ***/
struct editorConfig {
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow* row;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios;
    int tw;
};
struct editorConfig E;
/*** terminal ***/
void editorUpdateRow(erow* erow);

void editorAppendRow(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);
    E.numrows++;
    //fprintf(stderr, "appendd called\n");
}
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
    struct termios raw;
    if (tcgetattr (STDIN_FILENO, &E.orig_termios) < 0 ) die ("tcgetattr");
    atexit (disableRawMode);
    if ( tcgetattr (STDIN_FILENO, &raw)  < 0  ) die ("tcgetattr");
    raw.c_iflag &= ~ (BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_lflag &= ~ (OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~ (ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr (STDIN_FILENO, TCSAFLUSH, &raw) < 0 ) {
        fprintf (stderr, "Error: cannot set with tcsetattr due to '%s'\r\n",
                 strerror (errno));
        exit (1);
    }
}
/*** row operations ***/
void editorUpdateRow(erow *row) {
    free(row->render);
    int tabwidth = E.tw;
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t')  tabs++;
    }
    row->render = malloc(row->size + (tabwidth -1) * tabs + 1);
    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            assert(tabwidth > 0);
            for (int i = 0; i <  tabwidth; i++) {
                if (0)
                    row->render[idx++] = i%2 ? '^' : 'I';
                else
                    row->render[idx++] =' ';
            }
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}
int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (E.tw - 1);
        rx++;
    }
    //fprintf(stderr, "rx:%d\n",rx);
    return rx;
}
/*** input ***/
int
editorReadKey (void)
{
    char c;
    int nread;
    while ((nread = read (STDIN_FILENO, &c, 1)) != 1 ) {
        if (nread == -1 && errno != EAGAIN) die ("read");

    }

    if (c == '\x1b') {
        //fprintf(stderr, "hit esc seq");
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return (int)'\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return (int)'\x1b';
        if (seq[0] == '[') {
            switch (seq[1]) {
            case 'D':
                return ARROW_LEFT;
            case 'B':
                return ARROW_DOWN;
            case 'A':
                return ARROW_UP;
            case 'C':
                return ARROW_RIGHT;
            }
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
void
editorOpen (char *filename)
{
    free(E.filename);
    E.filename = strdup(filename);
    FILE *fp = fopen (filename, "r");
    if (!fp) die ("fopen");
    char *line = NULL;
    size_t linecap;
    ssize_t linelen;
    int x = 0;
    while ((linelen = getline (&line, &linecap, fp)) != -1 ) {
        while (linelen > 0  && (line[linelen - 1 ]  == '\r' ||
                                line[linelen - 1] == '\n') ) {
            linelen--;
        }
        x++;
        //fprintf(stderr, "line:%d\n", x);
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
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
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (c) {
    case ARROW_LEFT:
        if (E.cx != 0 )
            E.cx --;
        else if ( E.cx == 0) {
            E.cx = E.cy != 0 && row ? row->size : 0;
            E.cy = E.cy != 0 ? E.cy - 1 : 0;
        }
        break;
    case ARROW_UP:
        if (E.cy != 0)
            E.cy --;
        break;
    case ARROW_DOWN:
        if (E.cy <  E.numrows)
            E.cy ++;
        break;
    case ARROW_RIGHT:
        if (row &&  E.cx < row->size) {
            E.cx++;

        }
        else if (row && E.cx == row->size) {

            E.cy++;
            E.cx = 0;
        }
        break;
    }
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen)
        E.cx = rowlen;

}
void
editorProcessKey (void)
{
    int c = editorReadKey();
    //if (iscntrl (c)) {
    //    fprintf (stderr, "%d\r\n", c);
    //} else {
    //    fprintf (stderr, "%d ('%c')\r\n", c, c);
    //}
    switch (c) {
    case CTRLKEY ('q'):
        write (STDOUT_FILENO, "\x1b[2J", 4);
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
void editorScrool()
{
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorDrawStatusBar(struct ABUF *ab) {
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines",
                       E.filename ? E.filename : "[No Name]", E.numrows);
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
                        E.cy + 1, E.numrows);
    if (len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
}

void
editorSetStatusMessage(char *fmt, ...)
{
    va_list ap;
    va_start(ap,fmt);
    snprintf(E.statusmsg, sizeof(E.statusmsg),fmt,ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}
void
editorDrawRows (struct ABUF *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        //fprintf (stderr, "INFO: filerow: %d\n", filerow);
        if (y >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3 ) {
                char welcome[80];
                int welcomelen = snprintf (welcome, sizeof (welcome),
                                           "KILO editor-- version %s", KILO_VERSION);
                if (welcomelen > E.screencols ) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if (padding) {
                    abAppend (ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend (ab, " ", 1);
                abAppend (ab, welcome, welcomelen) ;
            } else {
                abAppend (ab, "~", 1) ;
            }
        } else {
            int len = E.row[filerow].rsize - 1 * E.coloff;
            if (len > E.screencols) len = E.screencols;
            abAppend (ab, &E.row[filerow].render[E.coloff], len);
        }
        abAppend (ab, "\x1b[K", 3) ;
        abAppend (ab, "\r\n", 2) ;

    }
}
void
editorDrawMessageBar(struct ABUF *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols;
    fprintf(stderr,"time:%ld\n", time(NULL) - E.statusmsg_time);
    if (msglen && (time(NULL) - E.statusmsg_time < 2)) {
        abAppend(ab, E.statusmsg, msglen);
    }
}
void
editorRefreshScreen (void)
{
    editorScrool();
    struct ABUF ab = ABUF_INIT;
    char cursor_buf[32];
    abAppend (&ab, "\x1b[?25l", 6);
    abAppend (&ab, "\x1b[H", 3);
    editorDrawRows (&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);
    snprintf (cursor_buf, sizeof (cursor_buf), "\x1b[%d;%dH", (E.cy- E.rowoff) + 1,( E.rx -E.coloff) + 1);
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
    E.tw = 4;
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    if (getWindowSize (&E.screenrows, &E.screencols) == -1 ) die ("getWindowSize");
    E.screenrows -= 2;
}
int
main (int argc, char *argv[])
{
    initEditor();
    lua_State* L =  luaL_newstate();
    luaL_openlibs(L);
// do something here...

    if (luaL_dofile(L, "script.lua") == LUA_OK) {
        fprintf(stderr,"[C] Executed script.lua\n");
        lua_getglobal(L, "tw"); // get tw on the stack
        if(!lua_isnumber(L, -1)) goto skip;
        
        int tw_in_c = lua_tonumber(L, -1); // tw is on top of the stack, use -1
        if (tw_in_c < 0) goto skip;
        fprintf(stderr,"[C] Received lua's tw with value %d\n", tw_in_c);
        E.tw = tw_in_c;
    } else {
         fprintf(stderr,"[C] Error reading script\n");
         luaL_error(L, "Error: %s\n", lua_tostring(L, -1));
    }
skip:
    lua_close(L);

    enableRawMode();
    if (argc == 2) {
        editorOpen(argv[1]);
    }
    editorSetStatusMessage("Help: CTRL-Q= quit");
    while (1) {
        editorRefreshScreen();
        editorProcessKey();
    }
    return 0;
}
