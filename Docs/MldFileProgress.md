# MLD File Progress

## Current Support

SPICE reads GameCube big-endian and Dreamcast little-endian MLD files, including AKLZ-wrapped GameCube input. It models the top-level index, counted link and address lists, entry transforms and names, texture lists, texture archives, and bounded raw payload blocks. GRND collision surfaces and a useful subset of GOBJ node and mesh data are decoded into semantic geometry. GameCube GVR and Dreamcast PVR archive entries are represented through their respective texture projects.

Known fields can be written in either platform byte order while unknown source bytes are preserved. The semantic writer supports texture replacement and archive relocation. Dreamcast GRND/GOBJ triangles also retain physical provenance for narrowly scoped selector edits.

## Known Limitations

MLD is not yet a complete semantic reassembler. NJCM/GJCM models and motion payloads are preserved or extracted but are not fully decoded by MLD itself. GRND and GOBJ support covers the promoted structures only, and several pointer, padding, and texture-control fields remain unnamed. Geometry edits outside the canonical GRND surface can therefore require raw preservation.

Triangle selector editing is limited to uncompressed Dreamcast files and does not determine whether a selector is meaningful for a particular area or object.
