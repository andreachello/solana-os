-- Themes: pick how Solana OS looks. The list names every theme the badge has;
-- the pane on the right previews the highlighted one in its own colours.
-- SELECT switches: the badge goes home in the new theme.
--
-- Needs a Solana OS with the `theme` module (docs/LUA_API.md "theme").

if not theme then
  ui.label(16, 70, "Themes needs a newer Solana OS.", {size=14, w=ui.W - 32, align="center"})
  ui.label(16, 96, "Update in Settings > Software update.", {size=12, color="dim", w=ui.W - 32, align="center"})
  ui.hint("CANCEL exit")
  return
end

local LIST_W = 168
local PX, PY = LIST_W + 4, 4                    -- the preview pane
local PW, PH = ui.W - PX - 4, ui.H - 16 - 8

local themes = theme.list()
local rows, current = {}, 1
for i, t in ipairs(themes) do
  rows[i] = {title = t.name, sub = t.about, right = t.on and "On" or ""}
  if t.on then current = i end
end

local list = ui.list(0, 4, LIST_W, ui.H - 20, rows, {on_select = function(i)
  local t = themes[i]
  if t.on then
    ui.toast(t.name .. " is on")
  else
    theme.set(t.id)                             -- the app closes; home opens in the new theme
  end
end})
list:select(current)
ui.hint("SELECT use   CANCEL exit")

-- The preview: a small screen drawn with the theme's own colours.
local pane = ui.rect(PX, PY, PW, PH, {color = 0, radius = 6, border = "border"})
local name = ui.label(PX + 10, PY + 8, "", {size = 14, w = PW - 20})
local card = ui.rect(PX + 10, PY + 34, PW - 20, 56, {color = 0, radius = 6})
local title = ui.label(PX + 20, PY + 42, "My Badge", {size = 14})
local sub = ui.label(PX + 20, PY + 64, "1,250 !LAMPs", {size = 12})
local sel = ui.rect(PX + 10, PY + 100, PW - 20, 24, {color = 0})
local selText = ui.label(PX + 20, PY + 104, "Selected", {size = 14})
local rest = ui.label(PX + 20, PY + 132, "Not selected", {size = 14})

local shown
local function preview(i)
  if i == shown or not themes[i] then return end
  shown = i
  local t = themes[i]
  pane:color(t.bg)
  name:text(t.name)        name:color(t.text)
  card:color(t.panel)
  title:color(t.text)      sub:color(t.dim)
  sel:color(t.focus)       selText:color(t.focus_text)
  rest:color(t.text)
end
preview(current)

-- The list takes up and down itself, so follow its selection.
function on_tick()
  preview(list:selected())
end
