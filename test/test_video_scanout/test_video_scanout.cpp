// test_video_scanout.cpp — Native unit tests for the video scanout pipeline
//
// These tests bypass the (real or stubbed) VGA controller / DMA / ISR pipeline
// and invoke the per-mode scanline rendering callback directly via the
// VideoScanout::renderScanline() test seam. They verify that:
//   - the adapter + scanout state plumbing is wired correctly,
//   - writes to video RAM produce expected pixel-level output,
//   - per-character pixel columns differ between distinct glyphs.
//
// Run with: pio test -e native -f test_video_scanout

#include <unity.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <initializer_list>

#include "video/video_scanout.h"
#include "video/cga.h"

using video::VideoScanout;
using video::CGA;

// 1 MB host RAM (matches RAM_SIZE in computer.h). CGA only touches the BIOS
// data area (around 0x400) when init/reset writes mode/equipment bytes.
static uint8_t test_ram[1048576];

// 80x25 text mode: pixelsLine=640, scanLines per callback=4.
// Buffer must hold one callback's worth: 640 * 4 = 2560 bytes.
static uint8_t pixel_buf[640 * 4];

static VideoScanout *scanout = nullptr;
static CGA *cga = nullptr;

static void scanout_setup() {
    if (!scanout) {
        scanout = new VideoScanout();
        scanout->init();
    }
    if (!cga) {
        cga = new CGA();
    }
    memset(test_ram, 0, sizeof(test_ram));
    memset(pixel_buf, 0, sizeof(pixel_buf));
    cga->init(test_ram, scanout); // also calls reset() → setMode(80x25 text) → run()
    // Clear CGA VRAM (CGA::init does not zero it on construction).
    memset(cga->vram(), 0, cga->vramSize());
    // Prime the scanline state cache (m_startAddress, m_cursor*, etc.) — only
    // refreshed when scanLine == 0 inside the callback.
    scanout->renderScanline(0, pixel_buf);
}

// Active text area starts at scanLine 20 in 80x25 text mode (top border = 20
// rows). Returns the 8 pixel bytes for the given (textCol, charScanline).
static void render_text_row0(int charScanline, uint8_t out_pixels_per_col[80][8]) {
    constexpr int pixelsLine = 640;
    const int scanLine = 20 + charScanline; // row 0, given char-internal line
    memset(pixel_buf, 0xAA, sizeof(pixel_buf)); // poison
    scanout->renderScanline(scanLine, pixel_buf);
    // The callback writes scanLines (4) consecutive lines of 640 pixels each
    // starting at dst. The first line corresponds to absolute scanLine.
    for (int col = 0; col < 80; col++) {
        for (int p = 0; p < 8; p++) {
            out_pixels_per_col[col][p] = pixel_buf[col * 8 + p];
        }
    }
}

// ── Tests ───────────────────────────────────────────────────────────────────

void test_scanout_default_mode_is_80x25_text() {
    scanout_setup();
    TEST_ASSERT_EQUAL_INT(640, scanout->width());
    TEST_ASSERT_EQUAL_INT(240, scanout->height());
    TEST_ASSERT_EQUAL_INT(4,   scanout->scanLinesPerCallback());
}

void test_scanout_border_above_active_area() {
    scanout_setup();
    // Scanline 0..19 are top border. Buffer should be uniform (all border color).
    memset(pixel_buf, 0xAA, sizeof(pixel_buf));
    scanout->renderScanline(0, pixel_buf);
    uint8_t border = pixel_buf[0];
    for (int i = 1; i < 640 * 4; i++) {
        if (pixel_buf[i] != border) {
            char msg[80];
            snprintf(msg, sizeof(msg), "border mismatch at %d: 0x%02x != 0x%02x",
                     i, pixel_buf[i], border);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

void test_scanout_border_below_active_area() {
    scanout_setup();
    memset(pixel_buf, 0xAA, sizeof(pixel_buf));
    scanout->renderScanline(220, pixel_buf);
    uint8_t border = pixel_buf[0];
    for (int i = 1; i < 640 * 4; i++) {
        TEST_ASSERT_EQUAL_HEX8(border, pixel_buf[i]);
    }
}

void test_scanout_blank_vram_yields_uniform_active_line() {
    scanout_setup();
    // VRAM is all zeros (char=0x00, attr=0x00 → fg=bg=black). Every pixel in
    // an active scanline must be the same value.
    uint8_t pixels[80][8];
    render_text_row0(0, pixels);
    uint8_t bg = pixels[0][0];
    for (int col = 0; col < 80; col++) {
        for (int p = 0; p < 8; p++) {
            TEST_ASSERT_EQUAL_HEX8(bg, pixels[col][p]);
        }
    }
}

void test_scanout_full_block_glyph_produces_solid_column() {
    scanout_setup();
    uint8_t *vram = cga->vram();
    // CP437 0xDB is the FULL BLOCK glyph: every pixel of every char-row is set.
    // Place it at column 0 with attr=0x07 (light gray on black).
    vram[0] = 0xDB;
    vram[1] = 0x07;

    // Test against char-scanlines 0, 3, 7 (top, mid, bottom of an 8-row glyph).
    for (int csl : {0, 3, 7}) {
        uint8_t pixels[80][8];
        render_text_row0(csl, pixels);

        // Column 0 (full block) must be a single solid color across all 8 px.
        uint8_t fg = pixels[0][0];
        for (int p = 1; p < 8; p++) {
            TEST_ASSERT_EQUAL_HEX8(fg, pixels[0][p]);
        }
        // Column 1 (char 0x00) is uniform background, distinct from full block.
        uint8_t bg = pixels[1][0];
        for (int p = 1; p < 8; p++) {
            TEST_ASSERT_EQUAL_HEX8(bg, pixels[1][p]);
        }
        TEST_ASSERT_NOT_EQUAL_MESSAGE(bg, fg,
            "foreground (full block) must differ from background");
    }
}

void test_scanout_distinct_chars_produce_distinct_columns() {
    scanout_setup();
    uint8_t *vram = cga->vram();
    // '.' (0x2E, mostly empty) vs '#' (0x23, dense pattern). Two glyphs with
    // very different bitmaps in any sane font: at least one of the 8 char rows
    // must produce different pixel patterns between the two columns.
    vram[0] = 0x2E; vram[1] = 0x07; // '.'
    vram[2] = 0x23; vram[3] = 0x07; // '#'

    bool any_row_differs = false;
    for (int csl = 0; csl < 8; csl++) {
        uint8_t pixels[80][8];
        render_text_row0(csl, pixels);
        for (int p = 0; p < 8; p++) {
            if (pixels[0][p] != pixels[1][p]) { any_row_differs = true; break; }
        }
        if (any_row_differs) break;
    }
    TEST_ASSERT_TRUE_MESSAGE(any_row_differs,
        "chars '.' and '#' must produce different pixel patterns in some row");

    // '#' must contain both fg and bg in at least one row (it's a dense glyph).
    bool any_two_colors = false;
    for (int csl = 0; csl < 8; csl++) {
        uint8_t pixels[80][8];
        render_text_row0(csl, pixels);
        uint8_t v = pixels[1][0];
        for (int p = 1; p < 8; p++) {
            if (pixels[1][p] != v) { any_two_colors = true; break; }
        }
        if (any_two_colors) break;
    }
    TEST_ASSERT_TRUE_MESSAGE(any_two_colors,
        "char '#' must produce a row with both fg and bg pixels");
}

void test_scanout_attribute_swap_inverts_glyph() {
    scanout_setup();
    uint8_t *vram = cga->vram();
    // Same char ('A'), opposite attributes:
    //   col 0: 0x07 = light-gray fg on black bg
    //   col 1: 0x70 = black fg on light-gray bg  (inverse video)
    vram[0] = 0x41; vram[1] = 0x07;
    vram[2] = 0x41; vram[3] = 0x70;

    uint8_t pixels[80][8];
    render_text_row0(2, pixels);

    // For an inverted attribute, every fg pixel becomes bg and vice versa.
    // So pixels[0][p] != pixels[1][p] for every position where the glyph has
    // a transition between fg and bg.
    int transitions = 0;
    for (int p = 0; p < 8; p++) {
        if (pixels[0][p] != pixels[1][p]) transitions++;
    }
    // 'A' mid-row has ≥1 fg pixel and ≥1 bg pixel → after inversion, every
    // pixel position differs between the two columns.
    TEST_ASSERT_EQUAL_INT_MESSAGE(8, transitions,
        "inverse attribute should flip every pixel of the glyph row");
}

// ── Main ────────────────────────────────────────────────────────────────────

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_scanout_default_mode_is_80x25_text);
    RUN_TEST(test_scanout_border_above_active_area);
    RUN_TEST(test_scanout_border_below_active_area);
    RUN_TEST(test_scanout_blank_vram_yields_uniform_active_line);
    RUN_TEST(test_scanout_full_block_glyph_produces_solid_column);
    RUN_TEST(test_scanout_distinct_chars_produce_distinct_columns);
    RUN_TEST(test_scanout_attribute_swap_inverts_glyph);
    int rc = UNITY_END();
    // Skip global destructors: cga/scanout cleanup involves the (stubbed) VGA
    // controller and ESP heap helpers and triggers a benign signal at process
    // teardown that confuses pio test's exit-code reporting.
    fflush(stdout);
    _exit(rc);
}
