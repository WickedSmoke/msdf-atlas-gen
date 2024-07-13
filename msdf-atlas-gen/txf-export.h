namespace msdf_atlas {

/// Writes the font and glyph metrics and atlas layout data into a binary TXF file
bool exportTXF(const FontGeometry *fonts, int fontCount,
               double fontSize, const msdfgen::Range& pixelRange,
               int atlasWidth, int atlasHeight, YDirection yDirection,
               const char *filename, bool kerning);

}
