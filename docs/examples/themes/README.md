# Themes: the theme picker as a store app

Lists every theme the badge's Solana OS has (`theme.list()`), previews the highlighted
one in its own colours, and switches to it on SELECT (`theme.set(id)`): the app closes
and the badge goes home in the new theme. The choice is the badge's Theme setting, the
same one Settings > Theme changes, so it stays after a restart.

- `main.lua`, `app.json`: the app. No permissions.

It needs a Solana OS that has the `theme` module ([LUA_API.md](../../LUA_API.md#theme-always-allowed)).
On an older one it says so and does nothing: the themes are part of the OS, an app can
only pick between the ones the badge has.
