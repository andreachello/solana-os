# Make this firmware yours

Nothing personal is built in. Your name and badge ID come from the badge
itself once it is claimed. The rest is set on the badge or over USB, and is
kept in the badge's settings, not in the code.

Over USB, `python3 tools/badge.py --port <port> "<command>"` sends one console
command (the port looks like `/dev/cu.usbserial-*` on a Mac).

## Theme

Settings, Theme. Or `theme` to list them and `theme <number>` to pick one.

## My Badge (Streaming theme)

| Key | What it does |
| --- | --- |
| Left, Right | The tile: picture, X, Telegram or a link, as a QR code |
| Down | Card view or split view (the tile as the left half) |
| Down on an empty QR tile | Type its handle or link |
| Hold Down on a QR tile | Change its handle or link |
| Up | Your event ticket, full screen |

The same from the console:

```
tile x yourhandle           your X profile
tile tg yourhandle          your Telegram
tile link https://your.site any link
tile pic                    back to the picture
tile                        what is set
```

## Event ticket

Scan your ticket's QR code with any QR reader to get the text inside it, then:

```
tile ticket <that text>
tile ticket-name "Your Name"   the name on the pass (default: the badge owner)
tile ticket off                remove it
```

## Your picture

The picture tile ships with a drawn face. To use a photo, before building:

```
python3 tools/set_picture.py photo.jpg            crop from the middle
python3 tools/set_picture.py avatar.png --circle  a round avatar: crop inside the circle
python3 tools/set_picture.py --default            the drawn face again
```

It rewrites `main/assets/img_me_card.c` and `main/assets/img_me_split.c`. To
keep your photo out of your commits:

```
git update-index --skip-worktree main/assets/img_me_card.c main/assets/img_me_split.c
```

## Try it without a badge

`emulator/local/build.sh` builds the firmware for the browser and serves it on
http://127.0.0.1:8765/. Add `?theme=13&server=none&badge=EMU001&claimed=@YourHandle&run=open%20badge` to see
a claimed badge; `tile=2&tg=yourhandle`, `split=1` and `ticket=<text>` set the
rest.
