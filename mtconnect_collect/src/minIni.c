/*  minIni.c - minimal INI parser, replacement implementation
 *
 *  The original minIni.c shipped in third_party is corrupted (binary).
 *  This file implements the subset of the minIni API used by the
 *  MTConnect fanuc adapter: ini_getl / ini_gets / ini_getkey
 *  (plus ini_getsection / ini_putl / ini_puts for completeness).
 *
 *  Same behavior as minIni v0.7 (ITB CompuPhase, Apache 2.0):
 *    - section headers  [Section]
 *    - key = value      (comments start with ';' or '#')
 *    - values may be quoted with " or '
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "minGlue.h"
#include "minIni.h"

#define INI_MAX_LINE  1024

static char *skip_ws(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static char *trim_right(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = '\0';
    return s;
}

/* case-insensitive compare of a section header "[Name]" with "Name" */
static int section_matches(const char *line, const char *section)
{
    if (*line != '[') return 0;
    line++;
    size_t n = strlen(section);
    if (strnicmp(line, section, n) != 0) return 0;
    line += n;
    while (*line == ' ' || *line == '\t') line++;
    return *line == ']';
}

int ini_getkey(const TCHAR *Section, int idx, TCHAR *Buffer, int BufferSize, const TCHAR *Filename)
{
    FILE *f = fopen(Filename, "rt");
    if (!f) return 0;

    char line[INI_MAX_LINE];
    int in_section = 0;
    int found = 0;

    while (fgets(line, sizeof(line), f)) {
        trim_right(line);
        char *s = skip_ws(line);
        if (*s == '\0' || *s == ';' || *s == '#') continue;

        if (*s == '[') {
            in_section = section_matches(s, Section);
            continue;
        }
        if (!in_section) continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim_right(s);

        if (found == idx) {
            strncpy(Buffer, key, BufferSize - 1);
            Buffer[BufferSize - 1] = '\0';
            fclose(f);
            return 1;
        }
        found++;
    }
    fclose(f);
    return 0;
}

int ini_gets(const TCHAR *Section, const TCHAR *Key, const TCHAR *DefValue,
             TCHAR *Buffer, int BufferSize, const TCHAR *Filename)
{
    FILE *f = fopen(Filename, "rt");
    if (!f) goto notfound;

    char line[INI_MAX_LINE];
    int in_section = 0;

    while (fgets(line, sizeof(line), f)) {
        trim_right(line);
        char *s = skip_ws(line);
        if (*s == '\0' || *s == ';' || *s == '#') continue;

        if (*s == '[') {
            in_section = section_matches(s, Section);
            continue;
        }
        if (!in_section) continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim_right(s);
        if (stricmp(key, Key) != 0) continue;

        char *val = skip_ws(eq + 1);
        /* strip quotes */
        if ((*val == '"' || *val == '\'') && strlen(val) >= 2) {
            char q = *val;
            val++;
            char *end = val + strlen(val) - 1;
            if (*end == q) *end = '\0';
        }
        strncpy(Buffer, val, BufferSize - 1);
        Buffer[BufferSize - 1] = '\0';
        fclose(f);
        return (int)strlen(Buffer);
    }
    fclose(f);

notfound:
    if (DefValue != NULL) {
        strncpy(Buffer, DefValue, BufferSize - 1);
        Buffer[BufferSize - 1] = '\0';
        return (int)strlen(Buffer);
    }
    Buffer[0] = '\0';
    return 0;
}

long ini_getl(const TCHAR *Section, const TCHAR *Key, long DefValue, const TCHAR *Filename)
{
    char buf[INI_BUFFERSIZE];
    ini_gets(Section, Key, "", buf, sizeof(buf), Filename);
    if (buf[0] == '\0') return DefValue;
    return strtol(buf, NULL, 10);
}

int ini_getsection(int idx, TCHAR *Buffer, int BufferSize, const TCHAR *Filename)
{
    FILE *f = fopen(Filename, "rt");
    if (!f) return 0;

    char line[INI_MAX_LINE];
    int found = 0;

    while (fgets(line, sizeof(line), f)) {
        trim_right(line);
        char *s = skip_ws(line);
        if (*s != '[') continue;
        char *end = strchr(s + 1, ']');
        if (!end) continue;
        if (found == idx) {
            size_t n = (size_t)(end - s - 1);
            if (n >= (size_t)BufferSize) n = BufferSize - 1;
            memcpy(Buffer, s + 1, n);
            Buffer[n] = '\0';
            fclose(f);
            return 1;
        }
        found++;
    }
    fclose(f);
    return 0;
}

int ini_putl(const TCHAR *Section, const TCHAR *Key, long Value, const TCHAR *Filename)
{
    char buf[32];
    sprintf(buf, "%ld", Value);
    return ini_puts(Section, Key, buf, Filename);
}

int ini_puts(const TCHAR *Section, const TCHAR *Key, const TCHAR *Value, const TCHAR *Filename)
{
    FILE *f = fopen(Filename, "a+t");
    if (!f) return 0;
    fprintf(f, "[%s]\n%s=%s\n", Section, Key, Value);
    fclose(f);
    return 1;
}
