#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "sched/io.h"

_Static_assert(sizeof(long long) == sizeof(int64_t), "strtoll must produce 64-bit values");

bool parse_i64(const char *s, int64_t *out) {
    if (!*s || isspace((unsigned char)*s)) return false;
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (errno == ERANGE || *end != '\0') return false;
    *out = (int64_t)v;
    return true;
}

/* ===================== interactive (whitespace) format ===================== */

enum { TOKEN_MAX = 64 };

/* Reads the next whitespace-delimited token: 1 on success, 0 at end of input,
 * -1 if the token does not fit in buf. */
static int next_token(FILE *in, char *buf, size_t size) {
    int c;
    do {
        c = fgetc(in);
    } while (c != EOF && isspace(c));
    if (c == EOF) return 0;
    size_t len = 0;
    while (c != EOF && !isspace(c)) {
        if (len + 1 >= size) return -1;
        buf[len++] = (char)c;
        c = fgetc(in);
    }
    buf[len] = '\0';
    return 1;
}

static SchedStatus read_int_token(FILE *in, const char *what, int64_t *out, SchedError *err) {
    char tok[TOKEN_MAX];
    int r = next_token(in, tok, sizeof tok);
    if (r == 0) {
        sched_error_set(err, "failed to read %s: unexpected end of input", what);
        return ferror(in) ? SCHED_E_IO : SCHED_E_INPUT;
    }
    if (r < 0) {
        sched_error_set(err, "failed to read %s: token too long", what);
        return SCHED_E_INPUT;
    }
    if (!parse_i64(tok, out)) {
        sched_error_set(err, "failed to read %s: '%s' is not a 64-bit integer", what, tok);
        return SCHED_E_INPUT;
    }
    return SCHED_OK;
}

SchedStatus read_workload_interactive(FILE *in, FILE *prompts, Workload *w, SchedError *err) {
    if (prompts) fputs("Number of Processes: ", prompts);
    int64_t n;
    SchedStatus st = read_int_token(in, "number of processes", &n, err);
    if (st != SCHED_OK) return st;
    if (n <= 0) {
        sched_error_set(err, "number of processes must be positive (got %" PRId64 ")", n);
        return SCHED_E_INPUT;
    }
    if (prompts)
        fputs("Enter details for each process on its own line: PID Arrival Burst\n", prompts);

    for (int64_t i = 1; i <= n; i++) {
        Process p;
        char what[64];
        snprintf(what, sizeof what, "PID of process #%" PRId64, i);
        if ((st = read_int_token(in, what, &p.pid, err)) != SCHED_OK) return st;
        snprintf(what, sizeof what, "arrival of process #%" PRId64, i);
        if ((st = read_int_token(in, what, &p.arrival, err)) != SCHED_OK) return st;
        snprintf(what, sizeof what, "burst of process #%" PRId64, i);
        if ((st = read_int_token(in, what, &p.burst, err)) != SCHED_OK) return st;
        if ((st = process_validate(&p, 0, err)) != SCHED_OK) return st;
        if ((st = workload_push(w, p, err)) != SCHED_OK) return st;
    }
    return SCHED_OK;
}

/* ===================== CSV format ===================== */

enum { LINE_MAX_LEN = 4096, MAX_COLUMNS = 16 };

/* Known columns. Optional columns added later (priority, tickets, ...) get a
 * row here with required = false and a default applied before parsing. */
typedef struct {
    const char *name;
    size_t offset; /* int64_t field inside Process */
    bool required;
} Column;

static const Column COLUMNS[] = {
    {"pid", offsetof(Process, pid), true},
    {"arrival", offsetof(Process, arrival), true},
    {"burst", offsetof(Process, burst), true},
};
enum { NUM_KNOWN_COLUMNS = sizeof COLUMNS / sizeof COLUMNS[0] };

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

/* Splits line in place on ','; returns the field count or -1 if > max. */
static int split_fields(char *line, char **fields, int max) {
    int count = 0;
    char *p = line;
    for (;;) {
        if (count == max) return -1;
        char *comma = strchr(p, ',');
        if (comma) *comma = '\0';
        fields[count++] = trim(p);
        if (!comma) return count;
        p = comma + 1;
    }
}

static int column_index(const char *name) {
    for (int i = 0; i < NUM_KNOWN_COLUMNS; i++) {
        const char *a = COLUMNS[i].name, *b = name;
        while (*a && tolower((unsigned char)*b) == *a) a++, b++;
        if (!*a && !*b) return i;
    }
    return -1;
}

static SchedStatus parse_header(char **fields, int count, int *col_of_field, size_t line,
                                SchedError *err) {
    bool seen[NUM_KNOWN_COLUMNS] = {false};
    for (int f = 0; f < count; f++) {
        int c = column_index(fields[f]);
        if (c < 0) {
            sched_error_set(err,
                            "line %zu: unknown column '%s' (the first line must be a header "
                            "such as pid,arrival,burst)",
                            line, fields[f]);
            return SCHED_E_INPUT;
        }
        if (seen[c]) {
            sched_error_set(err, "line %zu: column '%s' appears twice", line, COLUMNS[c].name);
            return SCHED_E_INPUT;
        }
        seen[c] = true;
        col_of_field[f] = c;
    }
    for (int c = 0; c < NUM_KNOWN_COLUMNS; c++) {
        if (COLUMNS[c].required && !seen[c]) {
            sched_error_set(err, "line %zu: header is missing required column '%s'", line,
                            COLUMNS[c].name);
            return SCHED_E_INPUT;
        }
    }
    return SCHED_OK;
}

static SchedStatus parse_row(char **fields, int count, const int *col_of_field, int header_count,
                             size_t line, Process *p, SchedError *err) {
    if (count != header_count) {
        sched_error_set(err, "line %zu: expected %d fields, found %d", line, header_count, count);
        return SCHED_E_INPUT;
    }
    for (int f = 0; f < count; f++) {
        const Column *col = &COLUMNS[col_of_field[f]];
        int64_t v;
        if (!parse_i64(fields[f], &v)) {
            sched_error_set(err, "line %zu: column '%s' must be a 64-bit integer (got '%s')", line,
                            col->name, fields[f]);
            return SCHED_E_INPUT;
        }
        memcpy((char *)p + col->offset, &v, sizeof v);
    }
    return process_validate(p, line, err);
}

SchedStatus read_workload_csv(FILE *in, Workload *w, SchedError *err) {
    char buf[LINE_MAX_LEN];
    char *fields[MAX_COLUMNS];
    int col_of_field[MAX_COLUMNS];
    int header_count = 0;
    size_t line = 0;

    while (fgets(buf, sizeof buf, in)) {
        line++;
        size_t len = strlen(buf);
        if (len == sizeof buf - 1 && buf[len - 1] != '\n' && !feof(in)) {
            sched_error_set(err, "line %zu: longer than %d characters", line, LINE_MAX_LEN - 2);
            return SCHED_E_INPUT;
        }
        char *text = buf;
        if (line == 1 && strncmp(text, "\xEF\xBB\xBF", 3) == 0) text += 3; /* UTF-8 BOM */
        text = trim(text);
        if (*text == '\0' || *text == '#') continue;

        int count = split_fields(text, fields, MAX_COLUMNS);
        if (count < 0) {
            sched_error_set(err, "line %zu: more than %d fields", line, MAX_COLUMNS);
            return SCHED_E_INPUT;
        }
        SchedStatus st;
        if (header_count == 0) {
            if ((st = parse_header(fields, count, col_of_field, line, err)) != SCHED_OK) return st;
            header_count = count;
            continue;
        }
        Process p = {0};
        if ((st = parse_row(fields, count, col_of_field, header_count, line, &p, err)) != SCHED_OK)
            return st;
        if ((st = workload_push(w, p, err)) != SCHED_OK) return st;
    }
    if (ferror(in)) {
        sched_error_set(err, "read error after line %zu", line);
        return SCHED_E_IO;
    }
    if (header_count == 0) {
        sched_error_set(err, "input is empty: expected a header line such as pid,arrival,burst");
        return SCHED_E_INPUT;
    }
    if (w->len == 0) {
        sched_error_set(err, "no processes listed after the header");
        return SCHED_E_INPUT;
    }
    return SCHED_OK;
}
