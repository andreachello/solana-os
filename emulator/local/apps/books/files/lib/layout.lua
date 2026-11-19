-- Page layout for Reader: wraps a book's text into lines with the OS fonts'
-- glyph widths (lib/metrics.lua), because a Lua app can't measure text and a
-- label holds 200 bytes at most. One label per line, placed by the app.
--
-- The text format (tools/gen_reader_books.py): a paragraph per line;
-- "# Kicker | Title" starts a chapter on a new page; a line that begins with a
-- space is verse, kept as a line of its own; "* * *" is centred; an empty
-- line is a small gap.

local M = {}
local byte, find, sub = string.byte, string.find, string.sub

function M.width(s, wt)
  local w = 0
  for i = 1, #s do w = w + (wt[byte(s, i)] or wt[63]) end
  return w
end

-- `s` cut to `width` px, ending in "..." when it didn't fit.
function M.fit(s, wt, width)
  if M.width(s, wt) <= width then return s end
  local w, dots = 0, 3 * wt[46]
  for i = 1, #s do
    w = w + (wt[byte(s, i)] or wt[63])
    if w + dots > width then return sub(s, 1, i - 1) .. "..." end
  end
  return s
end

-- `s` as lines of at most `width` px, broken at spaces.
function M.wrap(s, wt, width)
  local lines, line, w, sp = {}, nil, 0, wt[32]
  for word in s:gmatch("%S+") do
    local ww = M.width(word, wt)
    if line and w + sp + ww <= width then
      line, w = line .. " " .. word, w + sp + ww
    else
      if line then lines[#lines + 1] = line end
      line, w = word, ww
    end
  end
  if line then lines[#lines + 1] = line end
  return lines
end

-- A chapter heading: how tall it is and what it shows.
local function heading(text, fmt)
  local kicker, title = text:match("^%s*(.-)%s+|%s+(.-)%s*$")
  if not title then title = text:match("^%s*(.-)%s*$") end
  local lines = M.wrap(title, fmt.hwt, fmt.W)
  while #lines > 3 do lines[#lines] = nil end
  local h = 12 + (kicker and 16 or 0) + #lines * 24 + 20
  return {head = true, kicker = kicker, lines = lines, h = h}
end

-- Lays out one page from text[1..n], the text from the page's first byte on.
--   mid   the page starts inside a paragraph (no indent)
--   more  the text was cut short: there is more of the chapter after text[n]
--   fmt   { wt, hwt, W, H, lh, indent, gap }
--   want  build the items to draw (else only count)
-- Returns the bytes the page takes, the items and how many there are. Items are
-- { t = text, x =, y = } for a line and { head = true, kicker =, lines =, y =,
-- h = } for a chapter heading.
function M.page(text, n, mid, more, fmt, want)
  local wt, lh, W, H, indent = fmt.wt, fmt.lh, fmt.W, fmt.H, fmt.indent
  local sp, unknown = wt[32], wt[63]
  local items = want and {} or nil
  local count, y, p = 0, 0, 1
  local noindent = false

  while p <= n do
    local start = p                     -- where this paragraph begins
    local c = byte(text, p)
    local x, first, center = 0, 0, false
    if not mid then
      if c == 10 then                   -- an empty line: a gap, but not at the top
        p = p + 1
        if count > 0 then y = y + fmt.gap end
        goto continue
      elseif c == 35 then               -- "#": a chapter starts on a page of its own
        if count > 0 then break end
        local e = find(text, "\n", p, true)
        if not e or e > n then e = n + 1 end
        local h = heading(sub(text, p + 1, e - 1), fmt)
        h.y = y
        if want then items[#items + 1] = h end
        y, count, p, noindent = y + h.h, count + 1, e + 1, true
        goto continue
      elseif c == 32 then               -- verse
        x = indent
      elseif c == 42 and find(text, "^[%* ]+\n", p) then
        center = true
      elseif not noindent then
        first = indent
      end
    end

    do
      local done, lines = false, 0
      repeat
        while p <= n and byte(text, p) == 32 do p = p + 1 end
        if p > n then break end
        if y + lh > H then              -- the page is full
          return (lines == 0 and start or p) - 1, items, count
        end
        local avail = W - x - first
        local w, q, last, cut = 0, p, nil, false
        while true do
          local e = find(text, "[ \n]", q)
          if e and e > n then e = nil end
          if not e and more then cut = true break end     -- a word the text was cut in
          local stop = e and e - 1 or n
          local ww = 0
          for i = q, stop do ww = ww + (wt[byte(text, i)] or unknown) end
          local nw = last and (w + sp + ww) or ww
          if last and nw > avail then break end           -- the word starts the next line
          w, last = nw, stop
          if not e then q, done = n + 1, true break end
          q = e + 1
          if byte(text, e) == 10 then done = true break end
        end
        if last then
          if want then
            items[#items + 1] = {t = sub(text, p, last), x = center and (W - w) // 2 or x + first, y = y}
          end
          count, y, lines = count + 1, y + lh, lines + 1
        end
        p, first = q, 0
        if cut then return p - 1, items, count end
      until done
    end
    mid, noindent = false, false
    ::continue::
  end
  return (p > n and n or p - 1), items, count
end

return M
