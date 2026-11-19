# Books

A bookshelf and e-reader for the badge. Eleven public-domain books on a shelf of
covers (five classics of fiction, then six of philosophy from Descartes to Nietzsche),
pages you turn with LEFT and RIGHT, and your place kept for every book.
No permissions. Needs Solana OS 2.0.0 (the covers and page tables use about 850 KB
of Lua heap).

| Screen | Keys |
|---|---|
| Library | LEFT RIGHT browse, SELECT read, DOWN details, UP reading goals |
| Page | LEFT RIGHT (or UP DOWN) turn, SELECT menu, CANCEL library |
| Menu | Contents, bookmark this page, bookmarks, text size, page colour |
| Details | Continue, contents, bookmarks, about, start over, mark as finished |
| Goals | LEFT RIGHT set the daily goal (5 to 60 min) |

Three text sizes (Small, Medium, Large) and five page colours: Original follows the
badge's theme; Paper, White, Quiet and Night are fixed. The footer says how many pages
are left in the chapter and how far through the book you are. The streak needs the
badge to have had the time once (Wi-Fi).

## How it lays out a page

A Lua app can't measure text and a label holds 200 bytes, so the app wraps the lines
itself (`lib/layout.lua`) with the OS fonts' glyph widths (`lib/metrics.lua`, generated
from `main/assets/sol_*.c`) and shows one label per line. A chapter is cut into pages
from its first byte on, 2 KB of text per callback (about 35 000 of the 200 000
instructions), and the page table is kept, so going back is exact. A place in a book
is the byte its page starts at: it survives a change of text size.

## Books

`tools/gen_reader_books.py` writes everything bundled here: `books/<id>.txt` (a
paragraph per line, `# Kicker | Title` for a chapter, a leading space for verse),
`books/<id>.json` (title, author, chapters as byte offsets), the covers and the metrics.

```sh
python3 tools/gen_reader_books.py                  # rebuild the bundled books
python3 tools/gen_reader_books.py --add my.txt --title "My Book" --author "Me"
python3 tools/gen_reader_books.py --emulator       # copy the app to emulator/local/apps/books
```

A book you add gets a plain cover in its own colours. Text is turned into ASCII (the
badge's fonts have nothing else), and footnotes are dropped. A book over 250 KB is cut
into parts where a chapter starts (`<id>.txt`, `<id>.2.txt`), because a bundled file is
256 KB at most; the app reads them as one.

**Too big for the store as it is.** The store takes 64 files and 1 MB of them per app;
the eleven books and their covers are 49 files and 1.9 MB. The emulator and a card you
copy the app onto don't mind. To publish, remove books from `BOOKS` in the tool until
it reports under 1024 KB (the five classics alone are 658 KB), or publish the two
shelves as two apps.

The texts are from Project Gutenberg, in the public domain in the US, with the Project
Gutenberg header and licence removed: ebooks 11, 35, 43, 46 and 1952 (fiction) and
70091 (Descartes, in Molyneux's translation of 1680, the six Meditations without the
objections), 9662 (Hume), 5682 (Kant, tr. Abbott), 10732 (Schopenhauer, tr. Saunders),
11224 (Mill) and 4363 (Nietzsche, tr. Zimmern).
