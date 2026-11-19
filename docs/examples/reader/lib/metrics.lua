-- Glyph widths of the OS fonts in px, by byte (32..126). Made by
-- tools/gen_reader_books.py from main/assets/sol_*.c: don't edit.
local function widths(line, s)
  local t = {line = line}
  for i = 1, #s do t[31 + i] = s:byte(i) - 48 end
  return t
end
return {
  [12] = widths(13, [[55677875556756567677777777557777888887788577798878877889777565675777776775575977776767787766567]]),
  [14] = widths(14, [[34598;93446835369588888788338987;:9::98::4898=;;9;998:9>988464784788884883373<888857587;7765458]]),
  [20] = widths(21, [[557=<?=4668;5858<8<<<<<;<<55;<;:@>=??<<??6;=;C??=?==<>=D=<<686:<6:<;<;7<;55:5A;;<<7:7;:@::9757<]]),
}
