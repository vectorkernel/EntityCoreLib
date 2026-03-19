// hersheyfont.c (Demo3) - REPLACE ENTIRE FILE
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "hersheyfont.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Convert "Hershey-values" (offset by ASCII 'R') to integers
static inline short hershey_val(char c) { return (short)(c - 'R'); }

// Some .jhf files use the 5-digit glyph id as an index in [0..255].
// If it looks like that, we map directly. Otherwise we fall back to sequential mapping starting at ASCII 32.
static int parse_glyph_id_0_255(const char* line)
{
    if (!line) return -1;
    // Expect 5 digits at start
    char buf[6] = { 0 };
    memcpy(buf, line, 5);
    char* end = NULL;
    long v = strtol(buf, &end, 10);
    if (end == buf) return -1;
    if (v >= 0 && v <= 255) return (int)v;
    return -1;
}

static void free_glyph_paths(struct hershey_glyph* hg)
{
    if (!hg) return;
    struct hershey_path* p = hg->paths;
    while (p) {
        struct hershey_path* next = p->next;
        free(p);
        p = next;
    }
    hg->paths = NULL;
    hg->npaths = 0;
}

static int hershey_jhf_load_glyph(struct hershey_glyph* hg, char* jhf_line)
{
    if (!hg || !jhf_line) return 0;

    int len = (int)strlen(jhf_line);
    if (len <= 0) return 0;

    // Trim newline
    if (jhf_line[len - 1] == '\n') {
        jhf_line[len - 1] = 0;
        len--;
    }

    // Minimum: 5 glyphnum + 3 nverts + at least 2 chars
    if (len < 10) {
        errno = ERANGE;
        return 0;
    }

    errno = 0;

    char buf[8];
    char* end = NULL;

    // glyphnum (5 chars)
    memcpy(buf, jhf_line + 0, 5);
    buf[5] = 0;
    unsigned int glyphnum = (unsigned int)strtoul(buf, &end, 10);
    if (errno) return 0;

    // nverts (3 chars)
    memcpy(buf, jhf_line + 5, 3);
    buf[3] = 0;
    unsigned int nverts_raw = (unsigned int)strtoul(buf, &end, 10);
    if (errno) return 0;

    // Remaining length after first 8 chars should be nverts_raw*2
    int coord_bytes = len - 8;
    if ((int)(nverts_raw * 2) != coord_bytes) {
        errno = ERANGE;
        return 0;
    }

    // The first pair after the 8 header bytes encodes (leftpos,rightpos)
    // at indices 8 and 9 in the string.
    int leftpos = hershey_val(jhf_line[8]);
    int rightpos = hershey_val(jhf_line[9]);
    if (leftpos > rightpos) {
        errno = ERANGE;
        return 0;
    }

    // IMPORTANT: This matches your good log’s transform:
    //   x = xoffset + val(xchar)   where xoffset == rightpos
    //   y = yoffset - val(ychar)   where yoffset == 9
    const int xoffset = rightpos;
    const int yoffset = 9;

    // Skip over the (leftpos,rightpos) pair
    // The remaining vertices are (nverts_raw - 1) coordinate pairs.
    if (nverts_raw == 0) {
        errno = ERANGE;
        return 0;
    }

    unsigned int nverts = nverts_raw - 1;
    char* vertchars = jhf_line + 10;

    // Clear existing
    free_glyph_paths(hg);

    hg->glyphnum = glyphnum;
    hg->width = (unsigned int)(rightpos - leftpos);
    hg->npaths = 0;
    hg->paths = NULL;

    // --- Two-pass: count how many paths and how many verts per path ---
    // Pen-up marker is the pair: (' ', 'R')
    unsigned int path_count = 0;
    unsigned int cur_count = 0;

    for (unsigned int i = 0; i < nverts; ++i) {
        char cx = vertchars[i * 2 + 0];
        char cy = vertchars[i * 2 + 1];

        if (cx == ' ' && cy == 'R') {
            if (cur_count > 0) {
                path_count++;
                cur_count = 0;
            }
            continue;
        }
        cur_count++;
    }
    if (cur_count > 0) path_count++;

    hg->npaths = path_count;

    // Second pass: actually build linked list
    struct hershey_path* head = NULL;
    struct hershey_path* tail = NULL;

    unsigned int remaining = nverts;
    unsigned int i = 0;

    while (i < remaining) {
        // Skip any leading penups
        while (i < remaining) {
            char cx = vertchars[i * 2 + 0];
            char cy = vertchars[i * 2 + 1];
            if (cx == ' ' && cy == 'R') {
                i++;
                continue;
            }
            break;
        }
        if (i >= remaining) break;

        // Count verts in this path
        unsigned int start_i = i;
        unsigned int count = 0;
        while (i < remaining) {
            char cx = vertchars[i * 2 + 0];
            char cy = vertchars[i * 2 + 1];
            if (cx == ' ' && cy == 'R')
                break;
            count++;
            i++;
        }

        if (count == 0) {
            // Move past penup and continue
            if (i < remaining) i++;
            continue;
        }

        // Allocate path with flexible array size
        size_t bytes = sizeof(struct hershey_path) + (size_t)(count - 1) * sizeof(struct hershey_vertex);
        struct hershey_path* hp = (struct hershey_path*)malloc(bytes);
        if (!hp) return 0;

        hp->next = NULL;
        hp->nverts = count;

        // Fill vertices
        for (unsigned int k = 0; k < count; ++k) {
            char rx = vertchars[(start_i + k) * 2 + 0];
            char ry = vertchars[(start_i + k) * 2 + 1];

            // Apply the EXACT transform used by your working log
            short vx = (short)(xoffset + hershey_val(rx));
            short vy = (short)(yoffset - hershey_val(ry)); // Y flip

            hp->verts[k].x = vx;
            hp->verts[k].y = vy;
        }

        // Append
        if (!head) head = hp;
        else tail->next = hp;
        tail = hp;

        // Skip the penup marker (if present)
        if (i < remaining) {
            char cx = vertchars[i * 2 + 0];
            char cy = vertchars[i * 2 + 1];
            if (cx == ' ' && cy == 'R') i++;
        }
    }

    hg->paths = head;
    return 1;
}

static struct hershey_font* hershey_jhf_font_load(const char* jhffile)
{
    FILE* fp = fopen(jhffile, "r");
    if (!fp) return NULL;

    struct hershey_font* hf = (struct hershey_font*)calloc(1, sizeof(*hf));
    if (!hf) {
        fclose(fp);
        return NULL;
    }

    char linebuf[2048];
    int fallback_index = 32;

    while (fgets(linebuf, (int)sizeof(linebuf), fp)) {
        int glyph_id = parse_glyph_id_0_255(linebuf);

        int target_index = (glyph_id >= 0) ? glyph_id : fallback_index++;
        if (target_index < 0 || target_index > 255) break;

        struct hershey_glyph* hg = &hf->glyphs[target_index];
        // free if overwritten
        if (hg->paths) free_glyph_paths(hg);

        if (!hershey_jhf_load_glyph(hg, linebuf)) {
            hershey_font_free(hf);
            fclose(fp);
            return NULL;
        }
    }

    fclose(fp);
    return hf;
}



struct hershey_font* hershey_font_load(const char* fontname)
{
    const char* jhfdir = getenv("HERSHEY_FONTS_DIR");
    if (!jhfdir) jhfdir = "C:\\ProgramData\\hershey-fonts";

    size_t need = strlen(jhfdir) + 1 + strlen(fontname) + 4 + 1; // "/" + ".jhf" + null
    char* path = (char*)malloc(need);
    if (!path) return NULL;

    // Windows CRT accepts forward slashes fine
    sprintf(path, "%s/%s.jhf", jhfdir, fontname);

    // ---- DEBUG PRINT (after path is built) ----
    static int once = 0;
    if (!once)
    {
        once = 1;
        printf("hershey_font_load trying: %s\n", path);
    }

    struct hershey_font* hf = hershey_jhf_font_load(path);

    if (!hf)
    {
        printf("hershey_font_load FAILED (errno=%d)\n", errno);
    }

    free(path);
    return hf;
}

struct hershey_glyph* hershey_font_glyph(struct hershey_font* hf, unsigned int c)
{
    if (!hf) return NULL;
    if (c > 255) return NULL;
    return &hf->glyphs[c];
}

void hershey_font_free(struct hershey_font* hf)
{
    if (!hf) return;

    for (int i = 0; i < 256; ++i) {
        struct hershey_glyph* hg = &hf->glyphs[i];
        if (hg->paths) free_glyph_paths(hg);
    }
    free(hf);
}
