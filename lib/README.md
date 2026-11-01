# lib: reusable components (MIT)

The code in this folder is licensed under the **MIT License** (see [LICENSE](LICENSE); the header of each
file has the SPDX identifier `MIT`). It was written for Toji, but it has no part of Toji's user interface and does not use
MuPDF, so that SEN (enrichment plugins, navigators, ...) and other programs can use it without taking on the AGPL of Toji.

| File | What it does | Needs |
|------|--------------|-------|
| `WebAnnotation` | the W3C Web Annotation model in BMessages (`oa:`), JSON-LD, identifiers, selectors | Haiku API |
| `Bookmarks` | the bookmarks of a file as `oa:bookmarking` annotations in `SEN:annotations` | Haiku API, WebAnnotation |
| `EpubInfo` | what an EPUB says about itself (package document, cover) | libzip, libxml2 |
| `EpubCfi` | EPUB Canonical Fragment Identifiers: make and resolve | libzip, libxml2 |
| `ComicInfo` | `ComicInfo.xml` (ComicRack and the comic managers) | libxml2 |
| `BbfInfo` | the index of a Bound Book Format (`.bbf`) file | Haiku API |
| `SenStripeDialog.h` | the dialog with the stripe of Haiku's alerts (About, with the picture, frame, shadow and the full-size view); a copy of the header of [sento](https://github.com/sen-laboratories/sento) | Haiku API |

## The rule

**Nothing in this folder may include or link code with a copyleft license** (Toji's own sources, MuPDF, DjVuLibre, ...).
Everything it depends on is permissive (Haiku, libzip, libxml2). Toji (AGPL-3.0-or-later) uses these files; they do not use
Toji. A new file here gets the MIT header; a file that needs MuPDF stays in `src/`.

## Using it

Add the files you need to a Makefile (the headers are included as `"WebAnnotation.h"`, so give the folder as an include path).
`WebAnnotation::NewId()` makes the identifiers of annotations: TSIDs, compact time-sorted identifiers (13 characters of Base32).
