-- RideSync generated REAPER v1 marker import. Review positions below before running.
if not reaper.AddProjectMarker2 then error("REAPER AddProjectMarker2 unavailable") end
local function exists(name, position)
  local _, markers, regions = reaper.CountProjectMarkers(0)
  for i = 0, markers + regions - 1 do
    local ok, isregion, pos, _, oldname = reaper.EnumProjectMarkers3(0, i)
    if ok and not isregion and oldname == name and math.abs(pos-position) < 0.0000001 then return true end
  end
  return false
end
reaper.Undo_BeginBlock()
-- RideSync 7:1 [clip 1]: clip 0 s, project 30 s
if not exists("RideSync 7:1 [clip 1]", 30) then
  assert(reaper.AddProjectMarker2(0, false, 30, 0, "RideSync 7:1 [clip 1]", -1, 0) >= 0)
end
-- RideSync 7:2 [clip 1]: clip 2.5 s, project 32.5 s
if not exists("RideSync 7:2 [clip 1]", 32.5) then
  assert(reaper.AddProjectMarker2(0, false, 32.5, 0, "RideSync 7:2 [clip 1]", -1, 0) >= 0)
end
-- RideSync 7:3 [clip 1]: clip 4.5 s, project 34.5 s
if not exists("RideSync 7:3 [clip 1]", 34.5) then
  assert(reaper.AddProjectMarker2(0, false, 34.5, 0, "RideSync 7:3 [clip 1]", -1, 0) >= 0)
end
reaper.Undo_EndBlock("Import RideSync bookmarks", -1)
reaper.UpdateArrange()
