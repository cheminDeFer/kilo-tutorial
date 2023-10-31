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
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
/* Include the Lua API header files. */
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#define CTRLKEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"
#define KILO_QUIT_TIMES  3

enum editorKey {
    BACK_SPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_DOWN,
    ARROW_UP,
    ARROW_RIGHT,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
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
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios;
    int tw;
};
static bool editor_optws;
struct editorConfig E;
/*** terminal ***/
void editorSetStatusMessage(char *fmt, ...);
void editorUpdateRow(erow* erow);
char* editorPrompt(char* prompt, void(*callback)(char* query, int key) );
void editorFind(void);



void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);
    E.numrows++;
    E.dirty++;
    //fprintf(stderr, "appendd called\n");
}
void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
}
void
editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
    E.dirty++;
}
void editorInsertNewLine() {
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}
void
editorRowDelChar(erow* row, int at) {
    if(at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at+1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;

}
void editorRowFree(erow *row) {
    free(row->chars);
    free(row->render);
}
void editorDelRow(int at) {
    if( at < 0 || at >= E.numrows) return;
    editorRowFree(&E.row[at]);
    memmove(&E.row[at], &E.row[at+1], sizeof(erow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty++;

}
void editorRowAppendString(erow* row, char* src, int len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], src, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}
void editorDelChar() {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;
    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy -1].size;
        editorRowAppendString(&E.row[E.cy-1],row->chars,row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
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
        fprintf (stderr, "[C] Error: cannot set with tcsetattr due to '%s'\r\n",
                 strerror (errno));
        exit (1);
    }
}
/*** row operations ***/
void editorUpdateRow(erow *row) {
    free(row->render);
    int tabwidth = editor_optws ? 2 : E.tw;
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t')  tabs++;
    }
    int render_cap = row->size + (tabwidth -1) * tabs + 1;
    if (editor_optws) {
        render_cap++;
    }
    row->render = malloc(render_cap);
    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            assert(tabwidth > 0);
            for (int i = 0; i <  tabwidth; i++) {
                if (editor_optws)
                    row->render[idx++] = i%2 ? '^' : 'I';
                else
                    row->render[idx++] =' ';
            }
        }
        else {
            row->render[idx++] = row->chars[j];
        }
    }
    if (editor_optws) {
        row->render[idx++] = '$';
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}
int
editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += ( (editor_optws ?  2  : E.tw) - 1);
        rx++;
    }
    //fprintf(stderr, "rx:%d\n",rx);
    return rx;
}
int
editorRowRxToCx(erow *row, int rx) {
    int cx = 0;
    int j;
    int tabsize = editor_optws ?  2  : E.tw;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t')
            cx += ( (tabsize  - 1)   - (cx % tabsize ));
        cx++;
        if (cx > rx) return j;
    }
    //fprintf(stderr, "rx:%d\n",rx);
    return cx;
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
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return (int)'\x1b';
                if(seq[2] == '~') {
                    switch(seq[1]) {
                    case '1':
                        return HOME_KEY;
                    case '3':
                        return DEL_KEY;
                    case '4':
                        return END_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '7':
                        return HOME_KEY;
                    case '8':
                        return END_KEY;
                    }
                }

            }

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
void editorRefreshScreen(void);
char* editorPrompt(char* prompt, void (*callback )(char *query, int key ) ) {
    size_t bufsize = 128;
    char* buf = malloc(bufsize);
    if (buf == NULL) {
        fprintf(stderr, "ERROR: not enough memory at editorPrompt %s", strerror(errno));
        return NULL;
    }
    size_t buflen = 0;
    buf[0] = '\0';
    while(1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();
        int c = editorReadKey();
        if (c == DEL_KEY || c == CTRLKEY('h') || c == BACK_SPACE ) {
            if (buflen > 0) {
                buf[--buflen] = '\0';
            }
        }
        else if ( c == '\x1b') {
            editorSetStatusMessage("");
            if(callback) callback(buf, c);
            free(buf);
            return NULL;
        }
        else if (c == '\r') {
            if (buflen != 0) {
                editorSetStatusMessage("");
                if(callback) callback(buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
            if(callback) callback(buf, c);
        }
    }
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
        editorInsertRow(E.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
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
            if (E.cy == 0) {
                E.cy = 0;
                return;
            } else {
                E.cy =  E.cy - 1;

            }
            row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
            E.cx = (row) ? row->size : 0;
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
void lua_exec(lua_State *L);
void editorSave();
void editorRedraw();
void
editorProcessKey (lua_State *L)
{
    static int quit_times = KILO_QUIT_TIMES;
    int c = editorReadKey();
    switch (c) {
    case '\r':
        /* TODO */
        editorInsertNewLine();
        break;


    case CTRLKEY ('q'):
        if(E.dirty && quit_times > 0) {
            fprintf(stderr, "[INFO:] quit_times = %d\n",quit_times);
            editorSetStatusMessage(
                "Warning file has unsaved changes.Press Ctrl-Q %d more times",
                quit_times);
            quit_times--;
            return;
        }
        write (STDOUT_FILENO, "\x1b[2J", 4);
        exit (0);
        break;
    case CTRLKEY('e'):
        lua_exec(L);
        break;
    case CTRLKEY('s'):
        editorSave();
        break;
    case CTRLKEY('f'):
        editorFind();
        break;
    case HOME_KEY:
        E.cx = 0;
        break;
    case END_KEY:
        if (E.cy < E.numrows)
            E.cx = E.row[E.cy].size;
        break;
    case BACK_SPACE:
    case CTRLKEY('h'):
    case DEL_KEY:
        /* TODO */
        if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
        editorDelChar();
        break;
    case PAGE_UP:
    case PAGE_DOWN:
        /* TODO */
        break;

    case ARROW_LEFT:
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_RIGHT:
        editorMoveCursor (c);
        break;
    case CTRLKEY('l'):
    case '\x1b':
        editorRedraw();
        break;
    default:
        editorInsertChar( c );
        break;

    }
}

void editorRedraw(void) {
    for(int i = 0; i < E.numrows; i++) {
        editorUpdateRow(&E.row[i]);
    }
}

/*** find ***/

void editorFindCallback(char* query, int key);
void editorFind() {
    char *query = editorPrompt("Search %s (ESC to cancel.)",editorFindCallback);
    if(query) free(query);
}
void editorFindCallback(char* query, int key) {
    if( key == '\r' || key == '\x1b') {
        return;
    }

    for(int i=0; i< E.numrows; i++) {
        erow* row = &E.row[i];
        char *match = strstr(row->render, query);
        if (match) {
            E.cy = i;
            E.cx = editorRowRxToCx(row, match - row->render);
            //E.rowoff = E.numrows;
            break;
        }

    }
}
// lua api try
void lua_exec(lua_State *L) {
    char* execute = editorPrompt("lua execute %s (ESC to cancel", NULL);
    if (execute == NULL) return;
    fprintf(stderr,"executing prompt '%s'\n", execute);
    if(LUA_OK == luaL_dostring(L,execute)) {

        lua_getglobal(L, "k"); // get tw on the stack
        if (!lua_getfield(L, -1, "g")) {
            editorSetStatusMessage("cannot get g field in k");
        }
        if ( !lua_getfield(L, -1, "tw")) {
            fprintf(stderr,"[C] no field tw in k.g \n");
        }
        if(lua_isnumber(L,-1)) {
            editorSetStatusMessage("setting number to %d\n",(int)lua_tonumber(L, -1));

            E.tw = (int)lua_tonumber(L,-1);
        }
        else {

            editorSetStatusMessage("tw is not a number");
        }
        lua_pop(L,1);
        if(!lua_getfield(L,-1, "ws")) {
            editorSetStatusMessage("cannot get ws field in k.g");

        }
        if(lua_isboolean(L,-1)) {
            editor_optws = lua_toboolean(L,-1);
            fprintf(stderr,"Setting ws to %d\n", (bool)lua_toboolean(L,-1));

        }
        editorRedraw();
    }
    else {
        editorSetStatusMessage("Error running lua code <%s>\n",execute);
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
    int len = snprintf(status, sizeof(status), "%.20s%s - %d lines",
                       E.filename ? E.filename : "[No Name]",
                       E.dirty ? "*": "",E.numrows);
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
    abAppend(ab, "\r\n", 2);
}

void
editorSetStatusMessage(char *fmt, ...)
{
    va_list ap;
    va_start(ap,fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg),fmt,ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}
void
editorDrawRows (struct ABUF *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
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
    if (msglen && (time(NULL) - E.statusmsg_time < 5)) {
        abAppend(ab, E.statusmsg, msglen);
    } else {
        E.statusmsg[0] = '\0';
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
    abAppend (&ab, cursor_buf, strlen (cursor_buf));
    abAppend (&ab, "\x1b[?25h", 6);
    if ( write (STDOUT_FILENO, ab.b, ab.len ) != ab.len ) die ("cannot write ab");
    abFree (&ab);
}
/*** file i/o ***/
char *editorRowsToString(size_t buflen[static 1]) {
    size_t len = 0;
    for( int j = 0 ; j < E.numrows; j++) {
        len += E.row[j].size + 1; // +1 for newline
    }
    *buflen = len;
    char *buf = malloc(len ) ;
    char *p = buf;

    for( int j = 0 ; j < E.numrows; j++) {
        mempcpy( p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void editorSave() {
    if (E.filename == NULL) {
        E.filename = editorPrompt("Save as: %s", NULL);
        if (E.filename == NULL ) {
            editorSetStatusMessage("Save cancelled!.");
            return;
        }
    }
    size_t len;
    char *buf = editorRowsToString(&len);
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        fprintf( stderr, "Error at open %s\n", strerror( errno));
        editorSetStatusMessage("Error at open %s", strerror(errno));
        free(buf);
        return;
    }
    if (ftruncate( fd, len) < 0) {
        fprintf( stderr, "Error at ftruncate due to %s\n", strerror( errno));
        editorSetStatusMessage( "Error at ftruncate due to %s\n", strerror( errno));
    }
    if (write(fd, buf, len) < 0 ) {
        fprintf( stderr, "Error at write due to %s\n", strerror( errno));
        editorSetStatusMessage( "Error at write due to %s\n", strerror( errno));
        return;
    }
    close(fd);
    free(buf);
    editorSetStatusMessage("%d bytes written to disk", len);
    E.dirty = 0;

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
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    if (getWindowSize (&E.screenrows, &E.screencols) == -1 ) die ("getWindowSize");
    E.screenrows -= 2;
    editor_optws= false;
}
int
main (int argc, char *argv[])
{
    initEditor();
    editorSetStatusMessage("Help: CTRL-Q= quit CTRL-S= save CTRL-F=find");
    lua_State*L = luaL_newstate();
    luaL_openlibs(L);
    //do something here...
    lua_createtable(L,0,1);
    lua_setglobal(L,"k");
    lua_getglobal(L,"k");
    lua_createtable(L,0,1);
    lua_setfield(L,-2,"g");


    if (luaL_dofile(L, "script.lua") == LUA_OK) {
        fprintf(stderr,"[C] Executed script.lua\n");
        lua_getglobal(L, "k"); // get tw on the stack
        lua_getfield(L, -1, "g");
        if ( !lua_getfield(L, -1, "tw")) {
            fprintf(stderr,"[C] no field tw\n");
            goto skip;
        }


        if (lua_isnumber( L, -1) ) {
            fprintf(stderr,"[C] tw is number\n");
        } else {
            fprintf(stderr,"[C] tw is not a number\n");
        }
        if (!lua_isnumber(L, -1)) {
            fprintf(stderr,"[C] skipping bcause not number \n");
            goto skip;
        }

        int tw_in_c = lua_tonumber(L, -1); // tw is on top of the stack, use -1
        if (tw_in_c < 0) goto skip;
        fprintf(stderr,"[C] Received lua's tw with value %d\n", tw_in_c);
        E.tw = tw_in_c;
        lua_pop(L,1);
        if(!lua_getfield(L, -1, "ws")) {
            fprintf(stderr,"[C] no field ws\n");
            goto skip;
        }
        if (!lua_isboolean(L, -1)) {
            fprintf(stderr,"[C] skipping bcause not bool\n");
            goto skip;
        }

        fprintf(stderr,"[C] settin ws\n");
        editor_optws = lua_toboolean(L, -1);

    } else {
        fprintf(stderr,"[C] Error reading script\n");
        editorSetStatusMessage("Error: %s\n", lua_tostring(L, -1));
    }
skip:
    enableRawMode();
    if (argc == 2) {
        editorOpen(argv[1]);
    }
    while (1) {
        editorRefreshScreen();
        editorProcessKey(L);
    }
    lua_close(L);
    return 0;
}
