// hersheyfont.h (Demo3) - REPLACE ENTIRE FILE
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

    // One vertex in a Hershey path (Hershey coordinate units)
    struct hershey_vertex {
        short x;
        short y;
    };

    // A polyline path (linked list). Uses flexible array member for verts.
    struct hershey_path {
        struct hershey_path* next;
        unsigned int nverts;
        struct hershey_vertex verts[1];
    };

    // One glyph: width + linked list of paths
    struct hershey_glyph {
        unsigned int glyphnum;
        unsigned int width;          // advance width in Hershey units
        unsigned int npaths;
        struct hershey_path* paths;
    };

    // A loaded font: glyph table indexed by unsigned char [0..255]
    struct hershey_font {
        struct hershey_glyph glyphs[256];
    };

    // Load a .jhf font by name (e.g. "futural", "rowmans", etc).
    struct hershey_font* hershey_font_load(const char* fontname);

    // Free a font loaded by hershey_font_load.
    void hershey_font_free(struct hershey_font* hf);

    // Get glyph for a character code (0..255). Returns NULL if missing.
    struct hershey_glyph* hershey_font_glyph(struct hershey_font* hf, unsigned int c);

#ifdef __cplusplus
} // extern "C"
#endif

