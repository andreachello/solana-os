-- Books: a bookshelf and an e-reader for the badge. Covers on a shelf, pages
-- you turn with LEFT and RIGHT, and the place you stopped kept for every book.
-- Contents, bookmarks, three text sizes, five page colours, a daily reading
-- goal. No permissions.
--
-- The books are bundled files (books/, covers/), made by
-- tools/gen_reader_books.py, which also adds books of your own.

local layout = require("lib.layout")
local metrics = require("lib.metrics")

local W, H = ui.W, ui.H
local MX, TOP = 12, 8                   -- the page's margins
local TEXT_W, TEXT_H = W - 2 * MX, 190  -- the text block; the footer sits below it

local SIZES = {
  {name = "Small", size = 12, lh = 16, indent = 12},
  {name = "Medium", size = 14, lh = 18, indent = 14},
  {name = "Large", size = 20, lh = 25, indent = 18},
}
-- "Original" follows the badge's theme; the others are fixed, like paper is.
local PAPERS = {
  {name = "Original", bg = "bg", ink = "text", dim = "dim"},
  {name = "Paper", bg = 0xF3EAD7, ink = 0x2E261B, dim = 0x8C7E68},
  {name = "White", bg = 0xFFFFFF, ink = 0x18181C, dim = 0x8D8D96},
  {name = "Quiet", bg = 0x2B2B2E, ink = 0xD0D0D4, dim = 0x7C7C84},
  {name = "Night", bg = 0x000000, ink = 0x9A9AA6, dim = 0x4E4E58},
}
local GOALS = {5, 10, 15, 20, 30, 45, 60}
local CUT = 2000                        -- bytes of text cut into pages per callback: about 35 000 instructions
local RIBBON = {}                       -- a bookmark's ribbon, in the page's right margin
for i = 1, 16 do RIBBON[i] = i < 14 and "rrrrrrrr" or i == 14 and "rrr..rrr" or i == 15 and "rr....rr" or "r......r" end

-- ---------------------------------------------------------------------------
-- What is kept: prefs, the day's reading, and each book's place and bookmarks.

local function stored(key, default)
  local v = storage.get(key)
  return type(v) == "table" and v or default
end

local prefs = stored("prefs", {})
if not SIZES[prefs.size] then prefs.size = 2 end
if not PAPERS[prefs.paper] then prefs.paper = 1 end
if type(prefs.goal) ~= "number" then prefs.goal = 10 end
if type(prefs.seq) ~= "number" then prefs.seq = 0 end

local day = stored("day", {})
for _, k in ipairs({"d", "s", "streak", "total", "done"}) do
  if type(day[k]) ~= "number" then day[k] = 0 end
end
if type(day.hit) ~= "number" then day.hit = -1 end

local dirty = {}                        -- storage keys to write

local function flush()
  for key, value in pairs(dirty) do storage.set(key, value) end
  dirty = {}
end

local function today()
  local t = sys.time()
  return t and t // 86400 or 0          -- 0: the badge never had the time; one long day
end

local function roll_day()
  local d = today()
  if d ~= 0 and day.d ~= d then
    if day.hit < d - 1 then day.streak = 0 end
    day.d, day.s = d, 0
    dirty.day = day
  end
end

local function add_reading(seconds)
  if seconds <= 0 then return end
  roll_day()
  day.s, day.total = day.s + seconds, day.total + seconds
  if day.s >= prefs.goal * 60 and day.hit ~= day.d then
    day.streak = (day.hit == day.d - 1 and day.streak or 0) + 1
    day.hit = day.d
    ui.toast("Daily goal reached")
  end
  dirty.day = day
end

-- ---------------------------------------------------------------------------
-- The library

local books = {}
for _, f in ipairs(fs.list("books")) do
  local id = f:match("^(.+)%.json$")
  local meta = id and fs.json("books/" .. f)
  -- A long book comes in parts (a bundled file is 256 KB at most), cut where a
  -- chapter starts: <id>.txt, <id>.2.txt, ...
  local parts, size = {}, 0
  for i = 1, type(meta) == "table" and (meta.parts or 1) or 0 do
    local path = "books/" .. id .. (i > 1 and "." .. i or "") .. ".txt"
    local n = fs.size(path)
    if not n then parts, size = {}, 0 break end
    parts[i] = {path = path, at = size, size = n}
    size = size + n
  end
  if size > 0 then
    local b = {id = id, parts = parts, size = size, order = meta.order or 100 + #books,
               title = meta.title or id, author = meta.author or "", year = meta.year, about = meta.about,
               words = meta.words or size // 6, bg = meta.bg or 0x2A2935, ink = meta.ink or 0xFFFFFF,
               chapters = type(meta.chapters) == "table" and meta.chapters or {}}
    if not b.chapters[1] or b.chapters[1].o ~= 0 then
      table.insert(b.chapters, 1, {t = #b.chapters == 0 and b.title or "Beginning", o = 0})
    end
    b.st = stored("b_" .. id, {})
    if type(b.st.m) ~= "table" then b.st.m = {} end
    books[#books + 1] = b
  end
end

-- `len` bytes of a book from byte `off`, as far as the part that holds them goes.
local function read_book(b, off, len)
  for i = #b.parts, 1, -1 do
    local p = b.parts[i]
    if off >= p.at then return fs.read(p.path, off - p.at, math.min(len, p.size - (off - p.at))) or "" end
  end
  return ""
end

local function sort_books()             -- what you read last comes first
  table.sort(books, function(a, b)
    local an, bn = a.st.n or 0, b.st.n or 0
    if an ~= bn then return an > bn end
    return a.order < b.order
  end)
end
sort_books()

local function percent(b, off)
  return math.min(100, (off or b.st.o or 0) * 100 // b.size)
end

local function status(b)
  if b.st.f then return "Finished" end
  if (b.st.o or 0) > 0 then return percent(b) .. "%" end
  return "New"
end

local function duration(minutes)
  if minutes < 60 then return minutes .. " min" end
  local h, m = minutes // 60, minutes % 60
  return m >= 5 and (h .. " h " .. m .. " min") or (h .. " h")
end

local function chapter_at(b, off)
  local ci = 1
  for i, c in ipairs(b.chapters) do
    if c.o <= off then ci = i else break end
  end
  return ci
end

local function save_book(b)
  dirty["b_" .. b.id] = b.st
end

-- Covers are PNGs for the bundled books; any other book gets a plain one in
-- its own colours.
local function cover(b, x, y, big)
  local key = big and "cover" or "thumb"
  if b[key] == nil then
    local path = "covers/" .. b.id .. (big and "" or "_s") .. ".png"
    local ok, img = false, nil
    if fs.exists(path) then ok, img = pcall(ui.load, path) end
    b[key] = ok and img or false
  end
  local w, h = big and 88 or 60, big and 132 or 90
  if b[key] then
    ui.image(x, y, b[key])
  else
    ui.rect(x, y, w, h, {color = b.bg, radius = 2})
    ui.rect(x + 5, y, 1, h, {color = 0x000000})
    if big then
      ui.label(x + 12, y + 14, b.title, {size = 14, color = b.ink, w = w - 18})
      ui.label(x + 12, y + h - 22, layout.fit(b.author, metrics[12], w - 18), {size = 12, color = b.ink})
    end
  end
end

-- ---------------------------------------------------------------------------
-- Screens. `screen` names the one that is up; each show_* draws it from nothing.

local screen
local shelf = 1                         -- the book in the middle of the shelf
local current                           -- the book the details and reader screens are about
local show_library, show_detail, show_reader, show_menu, show_contents, show_marks

local function goal_line()
  roll_day()
  return "Today " .. day.s // 60 .. " of " .. prefs.goal .. " min"
end

function show_library()
  screen = "library"
  ui.clear()
  ui.bg("bg")
  ui.label(MX, 4, "Library", {size = 20})
  ui.label(W - MX - 170, 9, goal_line(), {size = 12, color = "dim", w = 170, align = "right"})
  if #books == 0 then
    ui.label(MX, 70, "No books on this badge.", {size = 14})
    ui.label(MX, 92, "Add some with tools/gen_reader_books.py", {size = 12, color = "dim"})
    ui.hint("CANCEL exit")
    return
  end
  if shelf > #books then shelf = #books end
  local base = 164                      -- the shelf the covers stand on
  for d = -2, 2 do
    local b = books[shelf + d]
    if b and d ~= 0 then cover(b, 160 + d * 74 - 30 + (d < 0 and -14 or 14), base - 90, false) end
  end
  local b = books[shelf]
  ui.rect(118, base - 130, 88, 132, {color = "border", radius = 2})     -- its shadow
  cover(b, 116, base - 132, true)
  ui.rect(0, base, W, 1, {color = "border"})
  ui.label(MX, base + 5, layout.fit(b.title, metrics[14], TEXT_W), {size = 14, w = TEXT_W, align = "center"})
  ui.label(MX, base + 23, b.author .. "  \xE2\x80\xA2  " .. status(b), {size = 12, color = "dim", w = TEXT_W, align = "center"})
  ui.hint("SELECT read   DOWN details   UP goals")
end

-- ---- Goals

local function show_goals()
  screen = "goals"
  ui.clear()
  ui.bg("bg")
  roll_day()
  local minutes = day.s // 60
  ui.label(MX, 4, "Reading Goals", {size = 20})
  ui.label(0, 38, tostring(minutes), {size = 32, w = W, align = "center", color = minutes >= prefs.goal and "green" or "text"})
  ui.label(0, 78, "of " .. prefs.goal .. " min today", {size = 14, color = "dim", w = W, align = "center"})
  ui.bar(60, 102, W - 120, 6, math.min(100, minutes * 100 // prefs.goal))
  local finished = 0
  for _, b in ipairs(books) do
    if b.st.f then finished = finished + 1 end
  end
  local rows = {
    {"Streak", today() == 0 and "needs the time" or (day.streak .. (day.streak == 1 and " day" or " days"))},
    {"Books finished", tostring(finished)},
    {"Time read", duration(day.total // 60)},
  }
  for i, r in ipairs(rows) do
    local y = 106 + i * 20
    ui.label(60, y, r[1], {size = 14})
    ui.label(140, y + 1, r[2], {size = 12, color = "dim", w = 120, align = "right"})
  end
  ui.hint("LEFT RIGHT daily goal   CANCEL back")
end

local function change_goal(step)
  local at = 1
  for i, g in ipairs(GOALS) do
    if g <= prefs.goal then at = i end
  end
  at = math.max(1, math.min(#GOALS, at + step))
  prefs.goal = GOALS[at]
  dirty.prefs = prefs
  show_goals()
end

-- ---- A book's details

local function show_about()
  screen = "about"
  ui.clear()
  ui.bg("bg")
  local b, y = current, 8
  for _, line in ipairs(layout.wrap(b.title, metrics[20], TEXT_W)) do
    ui.label(MX, y, line, {size = 20})
    y = y + 24
  end
  ui.label(MX, y + 2, b.author .. (b.year and "  \xE2\x80\xA2  " .. b.year or ""), {size = 12, color = "dim"})
  ui.label(MX, y + 26, b.about or "", {size = 14, w = TEXT_W})
  ui.hint("CANCEL back")
end

local open_book

function show_detail()
  screen = "detail"
  ui.clear()
  ui.bg("bg")
  local b = current
  local started = (b.st.o or 0) > 0 or b.st.f
  cover(b, MX, 10, true)
  ui.bar(MX, 150, 88, 4, b.st.f and 100 or percent(b))
  ui.label(MX, 160, status(b), {size = 12, color = "dim", w = 88, align = "center"})
  local x, y, w = 112, 8, W - 112 - 8
  local lines = layout.wrap(b.title, metrics[14], w)
  if #lines > 2 then lines[2] = layout.fit(lines[2] .. " " .. lines[3], metrics[14], w) end
  for i = 1, math.min(2, #lines) do
    ui.label(x, y, lines[i], {size = 14})
    y = y + 17
  end
  ui.label(x, y + 2, layout.fit(b.author, metrics[12], w), {size = 12, color = "dim"})
  ui.label(x, y + 18, (b.year and b.year .. "  \xE2\x80\xA2  " or "") .. duration(b.words // 230) .. " read", {size = 12, color = "dim"})

  local rows = {
    {title = b.st.f and "Read again" or started and "Continue" or "Read", right = started and not b.st.f and percent(b) .. "%" or "", act = "read"},
    {title = "Contents", right = #b.chapters, act = "contents"},
    {title = "Bookmarks", right = #b.st.m, act = "marks"},
  }
  if b.about and b.about ~= "" then rows[#rows + 1] = {title = "About", act = "about"} end
  if started and not b.st.f then rows[#rows + 1] = {title = "Start over", act = "restart"} end
  rows[#rows + 1] = {title = b.st.f and "Mark as unread" or "Mark as finished", act = "finish"}
  ui.list(104, 80, W - 104, 122, rows, {on_select = function(_, row)
    if row.act == "read" then
      open_book(b, b.st.f and 0 or b.st.o or 0)
    elseif row.act == "contents" then
      show_contents("detail")
    elseif row.act == "marks" then
      show_marks("detail")
    elseif row.act == "about" then
      show_about()
    elseif row.act == "restart" then
      ui.confirm("Go back to the first page of " .. b.title .. "?", function(yes)
        if yes then
          b.st.o, b.st.f = 0, nil
          save_book(b)
          show_detail()
        end
      end)
    elseif row.act == "finish" then
      if b.st.f then b.st.f, b.st.o = nil, 0 else b.st.f = true end
      save_book(b)
      show_detail()
    end
  end})
  ui.hint("SELECT choose   CANCEL library")
end

-- ---------------------------------------------------------------------------
-- The reader. A chapter is cut into pages from its first byte on, a few KB a
-- tick (a callback gets 200 000 instructions), and the page tables are kept
-- per chapter, so "back" is exact and the footer can say how many pages are
-- left. A place in a book is the byte its page starts at.

local R = {}                            -- the open book's state

local function new_format()
  local s = SIZES[prefs.size]
  R.fmt = {wt = metrics[s.size], hwt = metrics[20], W = TEXT_W, H = TEXT_H, lh = s.lh, indent = s.indent, gap = s.lh // 2}
  R.tables = {}                         -- chapter index -> { pages = {offsets}, next =, done = }
end

-- `len` bytes of the book from byte `off`, through one 8 KB block.
local function chunk(off, len)
  if not R.block or off < R.block_at or off + len > R.block_at + #R.block then
    R.block_at = off
    R.block = read_book(R.book, off, math.max(len, 8192))
  end
  local from = off - R.block_at
  return R.block:sub(from + 1, from + len)
end

local function chapter_span(ci)
  local ch = R.book.chapters
  return ch[ci].o, ch[ci + 1] and ch[ci + 1].o or R.book.size
end

-- The page that starts at byte `off` of chapter `ci`: where it ends, and what to draw.
local function lay_page(ci, off, want)
  local first, last = chapter_span(ci)
  local from = off > first and off - 1 or off         -- one byte back tells whether a paragraph starts here
  local text = chunk(from, math.min(2048 + off - from, last - from))
  local mid = false
  if from < off then
    mid = text:byte(1) ~= 10
    text = text:sub(2)
  end
  local n = #text
  local used, items, count = layout.page(text, n, mid, off + n < last, R.fmt, want)
  if used <= 0 then used = n end
  return off + math.max(1, used), items, count
end

local function page_table(ci)
  local t = R.tables[ci]
  if not t then
    local first = chapter_span(ci)
    t = {pages = {first}, next = first, done = false}
    R.tables[ci] = t
  end
  return t
end

-- Cuts more pages of chapter `ci`, about `budget` bytes of text.
local function paginate(ci, budget)
  local t = page_table(ci)
  local _, last = chapter_span(ci)
  while not t.done and budget > 0 do
    local stop = lay_page(ci, t.next, false)
    budget = budget - (stop - t.next)
    if stop >= last then
      t.done = true
    else
      t.pages[#t.pages + 1] = stop
      t.next = stop
    end
  end
  return t
end

local function page_end(ci, pi)
  local t = R.tables[ci]
  local _, last = chapter_span(ci)
  return t.pages[pi + 1] or (t.done and last) or nil
end

local function is_marked(from, to)
  for i, m in ipairs(R.book.st.m) do
    if m >= from and m < to then return i end
  end
end

local function count_time()
  local now = sys.millis()
  if R.shown then
    R.ms = (R.ms or 0) + math.min(now - R.shown, 90000)   -- a page left open isn't reading
    add_reading(R.ms // 1000)
    R.ms = R.ms % 1000
  end
  R.shown = now
end

local function draw_footer()
  local b, t = R.book, R.tables[R.ci]
  local left
  if t.done then
    local n = #t.pages - R.pi
    left = n == 0 and (R.ci == #b.chapters and "Last page" or "End of chapter")
      or n == 1 and "1 page left in chapter" or n .. " pages left in chapter"
  else
    left = layout.fit(b.chapters[R.ci].t, metrics[12], 200)
  end
  R.foot:text(left)
  local pct = percent(b, R.stop)
  R.pct:text(pct .. "%")
  R.prog:size(math.max(1, W * pct // 100), 2)
end

local function draw_page()
  local b = R.book
  local t = R.tables[R.ci]
  local off = t.pages[R.pi]
  local stop, items = lay_page(R.ci, off, true)
  R.off, R.stop = off, stop
  local li, hi = 0, 0
  R.kicker:show(false)
  R.rule:show(false)
  for _, it in ipairs(items) do
    if it.head then
      local y = TOP + it.y + 12
      if it.kicker then
        R.kicker:text(it.kicker:upper())
        R.kicker:pos(MX, y)
        R.kicker:show(true)
        y = y + 16
      end
      for _, line in ipairs(it.lines) do
        hi = hi + 1
        R.heads[hi]:text(line)
        R.heads[hi]:pos(MX, y)
        R.heads[hi]:show(true)
        y = y + 24
      end
      R.rule:pos(MX, y + 5)
      R.rule:show(true)
    elseif li < #R.lines then
      li = li + 1
      R.lines[li]:text(it.t)
      R.lines[li]:pos(MX + it.x, TOP + it.y)
      R.lines[li]:show(true)
    end
  end
  for i = li + 1, #R.lines do R.lines[i]:show(false) end
  for i = hi + 1, #R.heads do R.heads[i]:show(false) end
  R.ribbon:show(is_marked(off, stop) ~= nil)
  draw_footer()
  if b.st.o ~= off then
    b.st.o = off
    save_book(b)
  end
end

-- Shows the page of chapter `ci` that holds byte `off` (or its last page), once
-- the chapter is cut that far: at once if it is, else from on_tick.
local function resolve()
  local w = R.want
  if not w then return true end
  local t = paginate(w.ci, CUT)
  local n = #t.pages
  if w.next then                        -- the page after page `w.next`
    if n > w.next then
      R.ci, R.pi, R.want = w.ci, w.next + 1, nil
    elseif t.done then
      R.want = nil
      return true
    else
      return false
    end
  elseif w.last then
    if not t.done then return false end
    R.ci, R.pi, R.want = w.ci, n, nil
  else
    if not t.done and t.next <= w.off then return false end
    local pi = n
    while pi > 1 and t.pages[pi] > w.off do pi = pi - 1 end
    R.ci, R.pi, R.want = w.ci, pi, nil
  end
  draw_page()
  return true
end

local function go_to(off)
  off = math.max(0, math.min(off, R.book.size - 1))
  R.want = {ci = chapter_at(R.book, off), off = off}
  resolve()
end

local function finish_book()
  local b = R.book
  b.st.f, b.st.o = true, 0
  save_book(b)
  screen = "finished"
  ui.clear()
  ui.bg("bg")
  ui.label(0, 46, "The End", {size = 32, w = W, align = "center"})
  ui.label(MX, 96, "You finished", {size = 12, color = "dim", w = TEXT_W, align = "center"})
  ui.label(MX, 114, layout.fit(b.title, metrics[14], TEXT_W), {size = 14, w = TEXT_W, align = "center"})
  ui.label(MX, 134, "by " .. b.author, {size = 12, color = "dim", w = TEXT_W, align = "center"})
  ui.hint("SELECT library")
end

local function turn(step)
  if R.want then return end             -- still cutting the pages for the last key
  count_time()
  local t = R.tables[R.ci]
  if step > 0 then
    if R.pi < #t.pages then
      R.pi = R.pi + 1
      draw_page()
    elseif not t.done then
      R.want = {ci = R.ci, next = R.pi}
      resolve()
    elseif R.ci < #R.book.chapters then
      go_to(R.book.chapters[R.ci + 1].o)
    else
      finish_book()
    end
  elseif R.pi > 1 then
    R.pi = R.pi - 1
    draw_page()
  elseif R.ci > 1 then
    R.want = {ci = R.ci - 1, last = true}
    resolve()
  end
end

-- Builds the page's labels and shows the place the book is at.
function show_reader(off)
  screen = "reader"
  local paper, s = PAPERS[prefs.paper], SIZES[prefs.size]
  ui.clear()
  ui.bg(paper.bg)
  R.lines, R.heads = {}, {}
  for i = 1, TEXT_H // s.lh do
    R.lines[i] = ui.label(0, 0, "", {size = s.size, color = paper.ink})
    R.lines[i]:show(false)
  end
  R.kicker = ui.label(0, 0, "", {size = 12, color = paper.dim})
  for i = 1, 3 do
    R.heads[i] = ui.label(0, 0, "", {size = 20, color = paper.ink})
    R.heads[i]:show(false)
  end
  R.rule = ui.rect(0, 0, 28, 2, {color = paper.dim})
  R.foot = ui.label(MX, 202, "", {size = 12, color = paper.dim})
  R.pct = ui.label(W - MX - 60, 202, "", {size = 12, color = paper.dim, w = 60, align = "right"})
  R.prog = ui.rect(0, H - 2, 1, 2, {color = paper.dim})
  R.ribbon = ui.image(W - 10, 0, RIBBON, {palette = {r = 0xE5484D}})
  R.ribbon:show(false)
  R.kicker:show(false)
  R.rule:show(false)
  R.shown = sys.millis()
  go_to(off or R.book.st.o or 0)
end

function open_book(b, off)
  current = b
  R = {book = b}
  new_format()
  prefs.seq = prefs.seq + 1
  b.st.n = prefs.seq
  b.st.f = nil
  dirty.prefs = prefs
  save_book(b)
  show_reader(off)
end

local function close_book()
  count_time()
  R.shown, R.want = nil, nil
  flush()
  sort_books()
  shelf = 1
  show_library()
end

-- ---- The reader's menu

local function toggle_mark()
  local b = R.book
  local off = R.off
  local at = is_marked(off, R.stop)
  if at then
    table.remove(b.st.m, at)
  elseif #b.st.m >= 24 then
    ui.toast("24 bookmarks a book at most")
    return
  else
    b.st.m[#b.st.m + 1] = off
    table.sort(b.st.m)
  end
  save_book(b)
  ui.toast(at and "Bookmark removed" or "Page bookmarked")
end

function show_menu(select)
  screen = "menu"
  ui.clear()
  ui.bg("bg")
  local b, t = R.book, R.tables[R.ci]       -- no table: the text size just changed
  local off = R.off
  ui.label(MX, 5, layout.fit(b.title, metrics[14], TEXT_W), {size = 14})
  local dot = "  \xE2\x80\xA2  "
  local where = (#b.chapters > 1 and layout.fit(b.chapters[R.ci].t, metrics[12], 150) .. dot or "")
    .. (t and "Page " .. R.pi .. (t.done and " of " .. #t.pages or "") .. dot or "") .. percent(b, R.stop) .. "%"
  ui.label(MX, 23, where, {size = 12, color = "dim"})
  local rows = {
    {title = "Contents", right = R.ci .. " of " .. #b.chapters},
    {title = is_marked(off, R.stop) and "Remove bookmark" or "Bookmark this page"},
    {title = "Bookmarks", right = #b.st.m},
    {title = "Text size", right = SIZES[prefs.size].name},
    {title = "Page colour", right = PAPERS[prefs.paper].name},
    {title = "Back to library"},
  }
  local list = ui.list(0, 42, W, H - 16 - 42, rows, {on_select = function(i)
    if i == 1 then
      show_contents("menu")
    elseif i == 2 then
      toggle_mark()
      show_menu(2)
    elseif i == 3 then
      show_marks("menu")
    elseif i == 4 then
      prefs.size = prefs.size % #SIZES + 1
      dirty.prefs = prefs
      new_format()                      -- the pages are cut again, in the new size
      show_menu(4)
    elseif i == 5 then
      prefs.paper = prefs.paper % #PAPERS + 1
      dirty.prefs = prefs
      show_menu(5)
    else
      close_book()
    end
  end})
  if select then list:select(select) end
  ui.hint("SELECT choose   CANCEL back to the page")
end

-- ---- Contents and bookmarks, from the reader's menu or a book's details

local came_from

local function leave_list()
  if came_from == "menu" then show_menu() else show_detail() end
end

local function jump(off)
  if came_from == "menu" then
    count_time()
    show_reader(off)
  else
    open_book(current, off)
  end
end

function show_contents(from)
  screen, came_from = "contents", from
  ui.clear()
  ui.bg("bg")
  local b = current
  local rows = {}
  for i, c in ipairs(b.chapters) do
    rows[i] = {title = c.t, sub = c.k, right = percent(b, c.o) .. "%"}
  end
  local list = ui.list(0, 0, W, H - 16, rows, {on_select = function(i) jump(b.chapters[i].o) end})
  list:select(from == "menu" and R.ci or chapter_at(b, b.st.o or 0))
  ui.hint("SELECT go   CANCEL back")
end

local marks_list

function show_marks(from)
  screen, came_from = "marks", from
  ui.clear()
  ui.bg("bg")
  marks_list = nil
  local b = current
  if #b.st.m == 0 then
    ui.label(0, 70, "No bookmarks yet", {size = 14, w = W, align = "center"})
    ui.label(0, 92, "SELECT on a page, then Bookmark this page", {size = 12, color = "dim", w = W, align = "center"})
    ui.hint("CANCEL back")
    return
  end
  local rows = {}
  for i, off in ipairs(b.st.m) do
    local text = read_book(b, off, 64):gsub("^#%s*", ""):gsub("\n.*", ""):gsub("^%s+", "")
    rows[i] = {title = text, sub = b.chapters[chapter_at(b, off)].t, right = percent(b, off) .. "%"}
  end
  marks_list = ui.list(0, 0, W, H - 16, rows, {on_select = function(i) jump(b.st.m[i]) end})
  ui.hint("SELECT go   RIGHT remove   CANCEL back")
end

-- ---------------------------------------------------------------------------

function on_key(k, rep)
  if screen == "reader" then
    if k == "right" or k == "down" then
      turn(1)
    elseif k == "left" or k == "up" then
      turn(-1)
    elseif k == "select" then
      if not rep and not R.want then
        count_time()
        show_menu()
      end
    elseif not rep then
      close_book()
    end
    return true
  end
  if rep and k == "back" then return true end   -- a held CANCEL closes one screen, not all
  if screen == "library" then
    if k == "back" then return false end        -- exits
    if #books == 0 then return true end
    if k == "left" or k == "right" then
      local to = shelf + (k == "right" and 1 or -1)
      if to >= 1 and to <= #books then
        shelf = to
        show_library()
      end
    elseif k == "select" then
      local b = books[shelf]
      open_book(b, b.st.f and 0 or b.st.o or 0)
    elseif k == "down" then
      current = books[shelf]
      show_detail()
    elseif k == "up" then
      show_goals()
    end
  elseif screen == "goals" then
    if k == "back" then show_library()
    elseif k == "left" then change_goal(-1)
    elseif k == "right" then change_goal(1) end
  elseif screen == "detail" then
    if k == "back" then show_library() end
  elseif screen == "about" then
    if k == "back" then show_detail() end
  elseif screen == "menu" then
    if k == "back" then show_reader(R.off) end
  elseif screen == "contents" then
    if k == "back" then leave_list() end
  elseif screen == "marks" then
    if k == "back" then
      leave_list()
    elseif k == "right" and marks_list and not rep then
      local i = marks_list:selected()
      if i then
        table.remove(current.st.m, i)
        save_book(current)
        show_marks(came_from)
      end
    end
  elseif screen == "finished" then
    if k == "select" or k == "back" then close_book() end
  end
  return true
end

function on_tick()
  if screen ~= "reader" then return end
  if R.want then
    resolve()
  elseif R.tables[R.ci] and not R.tables[R.ci].done then
    paginate(R.ci, CUT)
    if R.tables[R.ci].done then draw_footer() end
  end
end

function on_exit()
  if screen == "reader" then count_time() end
  flush()
end

timer.every(15000, flush)
show_library()
