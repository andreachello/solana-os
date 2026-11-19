# Themes: your badge at a glance, in a look you pick

A store app that works on any badge with Solana OS 0.5.9 or later. Seven looks (Black
pearl, Pearl, Terminal, E-paper, Bold blocks, Monster RPG, Solana), each a full screen
showing the owner, the badge ID, whether the badge is online, and the battery. LEFT and RIGHT change the look; the choice is kept in the app's storage.

An app can only draw its own screen, so a look lasts while Themes is open: it cannot
restyle the home screen or other apps. On a Solana OS that has themes of its own (the
`theme` module, [LUA_API.md](../../LUA_API.md#theme-always-allowed)), SELECT also
switches the whole badge to the theme the look matches.

- `main.lua`, `app.json`: the app. No permissions.
- `art/black_pearl.png`, `art/pearl.png`: the pearl looks' backgrounds, 320x218 palette
  PNGs with the cards and artwork already on them (Lua images have no blending).
  `tools/gen_theme_faces.py` renders them.
