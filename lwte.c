#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

int visualX(char *line, int cx)
{
    int x = 0;

    for (int i = 0; i < cx; i++) {
        if (line[i] == '\t')
            x += 8 - (x % 8);
        else
            x++;
    }

    return x;
}

/* ===================== TERMINAL ===================== */

struct termios orig;

#define TEXT_START 2

void disableRaw() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
}

void enableRaw() {
    tcgetattr(STDIN_FILENO, &orig);
    atexit(disableRaw);

    struct termios raw = orig;

    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void clearScreen() {
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
}

/* ===================== FILE PROMPT ===================== */

char *promptFile() {
    clearScreen();

    write(STDOUT_FILENO,
        "LWTE - Enter file path (new or existing): ",
        43
    );

    char *buf = malloc(256);
    size_t i = 0;
    char c;

    while (i < 255) {
        if (read(STDIN_FILENO, &c, 1) != 1) continue;

        if (c == '\n' || c == '\r') break;

        if (c == 127) {
            if (i > 0) {
                i--;
                write(STDOUT_FILENO, "\b \b", 3);
            }
            continue;
        }

        buf[i++] = c;
        write(STDOUT_FILENO, &c, 1);
    }

    buf[i] = '\0';
    return buf;
}

/* ===================== FILE IO ===================== */

char **loadFile(const char *filename, int *rows) {
    FILE *fp = fopen(filename, "r");

    *rows = 0;

    char **buf = malloc(sizeof(char*));
    buf[0] = strdup("");

    if (!fp) {
        *rows = 1;
        return buf;
    }

    size_t cap = 8;
    buf = realloc(buf, sizeof(char*) * cap);

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, fp)) != -1) {
        if (*rows >= (int)cap) {
            cap *= 2;
            buf = realloc(buf, sizeof(char*) * cap);
        }

        if (read > 0 && line[read - 1] == '\n')
            line[read - 1] = '\0';

        buf[*rows] = strdup(line);
        (*rows)++;
    }

    free(line);
    fclose(fp);

    if (*rows == 0) {
        buf[0] = strdup("");
        *rows = 1;
    }

    return buf;
}

void saveFile(const char *filename, char **buf, int rows) {
    FILE *fp = fopen(filename, "w");
    if (!fp) return;

    for (int i = 0; i < rows; i++) {
        fwrite(buf[i], 1, strlen(buf[i]), fp);
        fwrite("\n", 1, 1, fp);
    }

    fclose(fp);
}

/* ===================== UI ===================== */

void drawUI(const char *file, int modified, int cy) {
    char bar[256];

    snprintf(bar, sizeof(bar),
        " LWTE 0.2 by 13bluepenguins | %s%s | Ctrl+F search within file | Ctrl+S save | Ctrl+Q quit | Line %d ",
        file,
        modified ? " *" : "",
	cy + 1
    );

    write(STDOUT_FILENO, "\x1b[1;1H", 6);
    write(STDOUT_FILENO, "\x1b[7m", 4);
    write(STDOUT_FILENO, bar, strlen(bar));
    write(STDOUT_FILENO, "\x1b[0m", 4);
}

/* ===================== BUFFER OPS ===================== */

void insertChar(char **buf, int cy, int cx, char c) {
    int len = strlen(buf[cy]);

    if (cx > len) cx = len;

    char *tmp = realloc(buf[cy], len + 2);
    if (!tmp) return;

    buf[cy] = tmp;

    for (int i = len; i >= cx; i--) {
        buf[cy][i + 1] = buf[cy][i];
    }

    buf[cy][cx] = c;
    buf[cy][len + 1] = '\0';
}

void insertNewline(char ***buf, int *rows, int *cy, int *cx) {
    char **b = *buf;

    int y = *cy;
    int x = *cx;

    int len = strlen(b[y]);
    if (x > len) x = len;

    char *left = strndup(b[y], x);
    char *right = strdup(b[y] + x);

    b = realloc(b, sizeof(char*) * (*rows + 1));

    for (int i = *rows; i > y + 1; i--) {
        b[i] = b[i - 1];
    }

    free(b[y]);

    b[y] = left;
    b[y + 1] = right;

    *buf = b;

    (*rows)++;
    (*cy)++;
    *cx = 0;
}

void backspace(char ***buf, int *rows, int *cy, int *cx) {
    char **b = *buf;

    if (*cx > 0) {
        memmove(&b[*cy][*cx - 1],
                &b[*cy][*cx],
                strlen(b[*cy]) - *cx + 1);
        (*cx)--;
        return;
    }

    if (*cy == 0) return;

    int prevLen = strlen(b[*cy - 1]);
    int curLen = strlen(b[*cy]);

    char *merged = malloc(prevLen + curLen + 1);
    strcpy(merged, b[*cy - 1]);
    strcat(merged, b[*cy]);

    free(b[*cy - 1]);
    free(b[*cy]);

    b[*cy - 1] = merged;

    for (int i = *cy; i < *rows - 1; i++) {
        b[i] = b[i + 1];
    }

    (*rows)--;
    (*cy)--;
    *cx = prevLen;
}

/* ===================== SEARCH ===================== */

int findText(char **buf, int rows, const char *query, int *outY, int *outX)
{
    if (query == NULL || strlen(query) == 0)
        return 0;

    for (int y = 0; y < rows; y++) {

        char *match = strstr(buf[y], query);

        if (match) {
            *outY = y;
            *outX = match - buf[y];
            return 1;
        }
    }

    return 0;
}

char *promptSearch()
{
    clearScreen();

    write(STDOUT_FILENO,
        "Search: ",
        8
    );

    char *buf = malloc(256);
    size_t i = 0;
    char c;

    while (i < 255) {

        if (read(STDIN_FILENO, &c, 1) != 1)
            continue;

        if (c == '\n' || c == '\r')
            break;

        if (c == 127) {
            if (i > 0) {
                i--;
                write(STDOUT_FILENO, "\b \b", 3);
            }
            continue;
        }

        buf[i++] = c;
        write(STDOUT_FILENO, &c, 1);
    }

    buf[i] = '\0';

    return buf;
}

/* ===================== RENDER ===================== */

void getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        *rows = 24;
        *cols = 80;
        return;
    }

    *rows = ws.ws_row;
    *cols = ws.ws_col;
}

void draw(char **buf, int rows, int cx, int cy, int rowoff, int coloff, const char *file, int mod) {
    clearScreen();
    drawUI(file, mod, cy);

    int screenrows;
    int screencols;

    getWindowSize(&screenrows, &screencols);

    for (int y = 0; y < screenrows - TEXT_START; y++) {

        int filerow = y + rowoff;

        char pos[32];
        snprintf(pos, sizeof(pos),
            "\x1b[%d;1H",
            y + TEXT_START
        );

        write(STDOUT_FILENO, pos, strlen(pos));
        write(STDOUT_FILENO, "\x1b[2K", 4);

	if (filerow < rows) {
		int len = strlen(buf[filerow]);

		if (coloff < len) {
			write(STDOUT_FILENO,
				buf[filerow] + coloff,
				len - coloff);
		}
	}
}

char cursor[32];

    snprintf(cursor, sizeof(cursor),
        "\x1b[%d;%dH",
        cy - rowoff + TEXT_START,
        visualX(buf[cy], cx) - coloff + 1
    );

    write(STDOUT_FILENO, cursor, strlen(cursor));
}

/* ===================== MAIN ===================== */

int main(int argc, char *argv[]) {
    enableRaw();

    char *file;

    if (argc > 1) {
        file = strdup(argv[1]);
    } else {
        file = promptFile();
        if (strlen(file) == 0) {
            free(file);
            file = strdup("untitled.txt");
        }
    }

    int rows = 0;
    char **buf = loadFile(file, &rows);

    int cx = 0, cy = 0;
    int rowoff = 0;
    int coloff = 0;
    int modified = 0;

    while (1) {

        if (cy < 0) cy = 0;
        if (cy >= rows) cy = rows - 1;

        if (cx < 0) cx = 0;
        if (cx > (int)strlen(buf[cy]))
            cx = strlen(buf[cy]);

        int screenrows;
        int screencols;

        getWindowSize(&screenrows, &screencols);

        if (cy < rowoff)
            rowoff = cy;

        if (cy >= rowoff + screenrows - TEXT_START)
            rowoff = cy - screenrows + TEXT_START + 1;

		int rx = visualX(buf[cy], cx);

		if (rx < coloff)
			coloff = rx;

		if (rx >= coloff + screencols)
			coloff = rx - screencols + 1;

        draw(buf, rows, cx, cy, rowoff, coloff, file, modified);

        char c;

        if (read(STDIN_FILENO, &c, 1) != 1)
            continue;

        /* Ctrl+F search */
        if (c == 6) {

            char *query = promptSearch();

            int sy;
            int sx;

            if (findText(buf, rows, query, &sy, &sx)) {
                cy = sy;
                cx = sx;

                coloff = visualX(buf[cy], cx) - screencols / 2;

                if (coloff < 0)
                    coloff = 0;
            }

            free(query);

            continue;
        }

        /* Arrow keys */
        if (c == '\x1b') {

            char seq[2];

            if (read(STDIN_FILENO, &seq[0], 1) != 1)
                continue;

            if (read(STDIN_FILENO, &seq[1], 1) != 1)
                continue;


            if (seq[0] == '[') {

                switch (seq[1]) {

                    case 'A':
                        if (cy > 0) cy--;
                        break;

                    case 'B':
                        if (cy < rows - 1) cy++;
                        break;

                    case 'C':
                        if (cx < (int)strlen(buf[cy]))
                            cx++;
                        break;

                    case 'D':
                        if (cx > 0)
                            cx--;
                        break;
                }
            }

            continue;
        }


        /* Ctrl+Q */
        if (c == 17)
            break;


        /* Ctrl+S */
        if (c == 19) {

            saveFile(file, buf, rows);
            modified = 0;

            continue;
        }


        /* Backspace */
        if (c == 127) {

            backspace(&buf, &rows, &cy, &cx);
            modified = 1;

            continue;
        }


        /* Enter */
        if (c == '\n' || c == '\r') {

            insertNewline(&buf, &rows, &cy, &cx);
            modified = 1;

            continue;
        }


        insertChar(buf, cy, cx, c);
        cx++;
        modified = 1;
    }

    clearScreen();

    for (int i = 0; i < rows; i++)
        free(buf[i]);

    free(buf);
    free(file);

    return 0;
}
