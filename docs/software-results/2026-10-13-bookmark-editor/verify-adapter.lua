-- Synthetic API contract check only. This does not run or emulate REAPER.
local script = assert(arg[1], "pass generated/ridesync-reaper-v1.lua")
local entries = {
  {region=false, position=5.25, finish=0, name="Existing marker KEEP", id=23, color=123},
  {region=true, position=1, finish=2, name="Existing region KEEP", id=24, color=456},
}
local adds = 0
local undo_depth = 0
local updates = 0
reaper = {}

function reaper.CountProjectMarkers(project)
  assert(project == 0)
  local markers, regions = 0, 0
  for _, entry in ipairs(entries) do
    if entry.region then regions = regions + 1 else markers = markers + 1 end
  end
  return #entries, markers, regions
end

function reaper.EnumProjectMarkers3(project, index)
  assert(project == 0)
  local entry = entries[index + 1]
  if not entry then return 0 end
  return 1, entry.region, entry.position, entry.finish, entry.name, entry.id, entry.color
end

function reaper.AddProjectMarker2(project, region, position, finish, name, wanted, color)
  assert(project == 0 and region == false and finish == 0 and wanted == -1 and color == 0)
  assert(undo_depth == 1)
  adds = adds + 1
  local id = 24 + adds
  entries[#entries + 1] = {
    region=region, position=position, finish=finish, name=name, id=id, color=color,
  }
  return id
end

function reaper.Undo_BeginBlock()
  assert(undo_depth == 0)
  undo_depth = 1
end

function reaper.Undo_EndBlock(name, flags)
  assert(undo_depth == 1 and name == "Import RideSync bookmarks" and flags == -1)
  undo_depth = 0
end

function reaper.UpdateArrange()
  assert(undo_depth == 0)
  updates = updates + 1
end

for run = 1, 2 do
  dofile(script)
  local total, markers, regions = reaper.CountProjectMarkers(0)
  assert(total == 5 and markers == 4 and regions == 1 and adds == 3)
  assert(updates == run and undo_depth == 0)
  local expected = {30, 32.5, 34.5}
  for id, position in ipairs(expected) do
    local entry = entries[id + 2]
    assert(entry.position == position and entry.name == "RideSync 7:" .. id .. " [clip 1]")
    assert(not entry.region and entry.finish == 0 and entry.color == 0)
  end
  local marker, region = entries[1], entries[2]
  assert(marker.position == 5.25 and marker.name == "Existing marker KEEP")
  assert(marker.id == 23 and marker.color == 123 and marker.finish == 0 and not marker.region)
  assert(region.position == 1 and region.finish == 2 and region.name == "Existing region KEEP")
  assert(region.id == 24 and region.color == 456 and region.region)
end
print("PASS synthetic Lua API contract: exact three positions/names, existing entries preserved, repeat adds zero")
