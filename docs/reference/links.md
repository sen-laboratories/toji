# Links to places

A link is a URI that opens a document at an exact place. Programs hand them around as text (a message, a note, a web page), and
Toji makes them from what you are looking at.

> **Note:** a `toji:` link holds the path of the file, which is brittle when files are moved. Stable links that name a document by
> its SEN:ID (TSID) will be a `sen://` handler in SEN; it will pass the place (the fragment below) to Toji, which opens it as it does
> here. The fragment is the part that stays.

```
toji:///boot/home/papers/x.pdf#page=5
toji:///boot/home/papers/x.pdf#page=5&xywh=percent:10,20,30,40
toji:///boot/home/papers/x.pdf#page=5:~:text=the%20words
toji:///boot/home/books/a%20book.epub#epubcfi(/6/4[chap01]!/4/10)
toji:///boot/home/papers/x.pdf#annotation=02SEHT4FEMN73
```

The part before `#` is the file: an absolute path, percent-encoded (`%20` for a space). A `file:` URI or a plain path with the
same `#` part works as well.

## The fragment

It uses the fragment identifiers that exist for this, so the places mean the same as the selectors of the
[Web Annotation model](annotations.md#the-web-annotation-model) that Toji keeps annotations in:

| Fragment | Standard | Place |
|----------|----------|-------|
| `page=5` | RFC 3778 | the page (1-based) |
| `xywh=percent:x,y,w,h` | W3C Media Fragments | a region of the page, in percent of it (`oa:FragmentSelector`) |
| `epubcfi(...)` | EPUB Canonical Fragment Identifier | a place in an EPUB, whatever the text size |
| `:~:text=[prefix-,]words[,-suffix]` | Text Fragments | the words (`oa:TextQuoteSelector`); a comma or hyphen in the words is encoded |
| `annotation=<tsid>` | (Toji's term) | an annotation, by its identifier (see [annotations.md](annotations.md#identifiers)) |

`page=5&xywh=...` combines, and the text directive comes last: `page=5:~:text=...`.

## Using them

- **Open one:** `Toji 'toji:///boot/home/papers/x.pdf#page=5'`, or click it in a program that passes `toji:` links to the
  system (`open toji:///...` does). Toji is the program for the scheme (the type `application/x-vnd.Be.URL.toji`).
- **Make one:** **Edit > Copy link to this place** (Cmd+Shift+L), or the same entry in the menu of the secondary mouse button, puts
  a link on the clipboard: to the selected annotation, to the selected words, or to the page that is shown (in a book, to its
  place in the text).
- **From a program:** the scripting property `Link` of the document, and `Goto` with a `uri` (see [scripting.md](scripting.md));
  in code, `lib/DeepLink.h` (MIT) makes and reads the links.
- **As a message:** a `B_REFS_RECEIVED` message with the file in `refs` and an `oa:hasTarget` (a message with `oa:hasSelector` entries as above)
  goes to the place. With `oa:motivatedBy` (for example `oa:highlighting`) the words of a quote, or the region of a place, are marked for three
  seconds as a hint to the reader: it is no annotation, the document is not changed, and nothing asks to be saved at quit.
