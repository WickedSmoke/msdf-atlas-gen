#include <algorithm>
#include "FontGeometry.h"
#include "txf-export.h"

struct TxfHeader {
    uint16_t texW;
    uint16_t texH;
    uint16_t glyphCount;
    uint16_t kernOffset;
    float fontSize;
    float pixelRange;
    float lineHeight;
    float ascender;
    float descender;
};

struct TxfGlyph {
    uint16_t code;
    uint16_t kernIndex;
    float advance;
    float emRect[4];
    float tcRect[4];
};

struct KernSortEntry {
    uint32_t codeL;
    uint32_t codeR;
    union {
        float f;
        uint32_t u;
    } advance;
};

namespace msdf_atlas {

static void txf_setKernIndex(TxfGlyph* gbuf, int count, uint16_t code,
                             uint16_t index) {
    TxfGlyph* end = gbuf + count;
    for (; gbuf != end; ++gbuf) {
        if (gbuf->code == code) {
            gbuf->kernIndex = index;
            break;
        }
    }
}

bool exportTXF(const FontGeometry *fonts, int fontCount,
               double fontSize, double pixelRange,
               int atlasWidth, int atlasHeight, YDirection yDirection,
               const char *filename, bool kerning) {
    TxfHeader hdr;
    char outfile[512];
    std::vector<uint32_t> kernTable;
    uint32_t highCode;
    bool ok = true;
    FILE* f;

    for (int i = 0; i < fontCount && ok; ++i) {
        const FontGeometry& font = fonts[i];
        strcpy(outfile, filename);
        if (fontCount > 1) {
            const char* name = font.getName();
            char* base = strstr(outfile, ".txf");
            if (! base)
                base = outfile + strlen(outfile);
            if (name)
                sprintf(base, "-%s.txf", name);
            else
                sprintf(base, "-%d.txf", i);
        }

        f = fopen(outfile, "wb");
        if (! f)
            return false;

        FontGeometry::GlyphRange glyphs = font.getGlyphs();

        {
        const msdfgen::FontMetrics& metrics = font.getMetrics();

        hdr.texW = atlasWidth;
        hdr.texH = atlasHeight;
        hdr.glyphCount  = 0;
        hdr.kernOffset  = 0;
        hdr.fontSize    = fontSize;
        hdr.pixelRange  = pixelRange;
        hdr.lineHeight  = metrics.lineHeight;
        hdr.ascender    = metrics.ascenderY;
        hdr.descender   = metrics.descenderY;
        }

        {
        //int x, y, w, h;
        double l, b, r, t;
        double aw = double(atlasWidth);
        double ah = double(atlasHeight);
        uint32_t code, lowCode;

        lowCode = 0xffff;
        highCode = 0;
        for (const GlyphGeometry& gg : glyphs) {
            code = gg.getCodepoint();
            if (code > 0xffff)
                continue;               // Only handle UCS-2.
            if (code < lowCode)
                lowCode = code;
            if (code > highCode)
                highCode = code;
        }

        hdr.glyphCount = highCode - lowCode + 1;

        TxfGlyph* gbuf = new TxfGlyph[ hdr.glyphCount ];
        memset(gbuf, 0, hdr.glyphCount * sizeof(TxfGlyph));

        for (const GlyphGeometry& gg : glyphs) {
            code = gg.getCodepoint();
            if (code > 0xffff)
                continue;               // Only handle UCS-2.

            TxfGlyph* gi = gbuf + (code - lowCode);

            gi->code      = code;
            gi->kernIndex = 0;
            gi->advance   = gg.getAdvance();
#if 0
            gg.getBoxRect(x, y, w, h);
            gi->rect[0] = double(x) / aw;
            gi->rect[1] = double(y) / ah;
            gi->rect[2] = double(x+w) / aw;
            gi->rect[3] = double(y+h) / ah;
#else
            gg.getQuadPlaneBounds(l, b, r, t);
            gi->emRect[0] = l;
            gi->emRect[1] = b;
            gi->emRect[2] = r;
            gi->emRect[3] = t;

            gg.getQuadAtlasBounds(l, b, r, t);
            gi->tcRect[0] = l / aw;
            gi->tcRect[1] = b / ah;
            gi->tcRect[2] = r / aw;
            gi->tcRect[3] = t / ah;
#endif

#if 0
            printf("KR '%c': %f (%f %f %f %f) (%f %f %f %f)\n",
                   gi->code, gi->advance,
                   gi->emRect[0], gi->emRect[1], gi->emRect[2], gi->emRect[3],
                   gi->tcRect[0], gi->tcRect[1], gi->tcRect[2], gi->tcRect[3]);
#endif
        }

        if (kerning) {
            std::vector<KernSortEntry> ksort;

            for (const std::pair<std::pair<int, int>, double> &kernPair :
                    font.getKerning()) {
                const GlyphGeometry* geoL =
                    font.getGlyph(msdfgen::GlyphIndex(kernPair.first.first));
                const GlyphGeometry* geoR =
                    font.getGlyph(msdfgen::GlyphIndex(kernPair.first.second));
                if (geoL && geoR &&
                    geoL->getCodepoint() && geoR->getCodepoint()) {
                    KernSortEntry ent;
                    ent.codeL = geoL->getCodepoint();
                    ent.codeR = geoR->getCodepoint();

                    // Only handle UCS-2.
                    if (ent.codeL <= 0xffff && ent.codeR <= highCode) {
                        ent.advance.f = kernPair.second;
                        ksort.push_back(ent);
                    }
                }
            }

            if (! ksort.empty()) {
                sort(ksort.begin(), ksort.end(),
                    [](const KernSortEntry& e1, const KernSortEntry& e2) {
                        if (e1.codeL < e2.codeL)
                            return true;
                        if (e1.codeL == e2.codeL && e1.codeR < e2.codeR)
                            return true;
                        return false;
                    });

                // Number of uint32_t.
                hdr.kernOffset = (sizeof(TxfHeader) +
                                  (sizeof(TxfGlyph) * hdr.glyphCount)) / 4;

                uint32_t curCode = 0xffffffff;
                for (const KernSortEntry& it : ksort) {
                    if (curCode != it.codeL) {
                        curCode = it.codeL;
                        kernTable.push_back(0);
                        txf_setKernIndex(gbuf, hdr.glyphCount, curCode,
                                         kernTable.size());
                    }
#if 0
                    printf("KR kern '%c' '%c' %f\n",
                            it.codeL, it.codeR, it.advance.f);
#endif
                    kernTable.push_back(it.codeR);
                    kernTable.push_back(it.advance.u);
                }
                kernTable.push_back(0);
            }
        }

        if (fwrite(&hdr, 1, sizeof(TxfHeader), f) == sizeof(TxfHeader)) {
            if (fwrite(gbuf, sizeof(TxfGlyph), hdr.glyphCount, f) ==
                hdr.glyphCount) {
                if (! kernTable.empty())
                    fwrite(kernTable.data(), sizeof(uint32_t),
                           kernTable.size(), f);
            } else
                ok = false;
        } else
            ok = false;

        delete[] gbuf;
        kernTable.clear();
        }

        fclose(f);
    }
    return ok;
}

}
