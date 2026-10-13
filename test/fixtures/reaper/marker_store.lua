-- Test-only storage implementing the documented REAPER marker API boundary.
-- Expected placement and preservation assertions live in Python, not this store.
local adapter, scenario = arg[1], arg[2]
local entries
if scenario == 'preservation' then
  entries = {
    {false, 5.25, 0, 'Existing marker KEEP', 23, 123},
    {true, 1, 2, 'Existing region KEEP', 24, 456},
  }
elseif scenario == 'collisions' then
  entries = {
    {false, 32.5, 0, 'RideSync 7:1 [clip 1]', 23, 123},
    {true, 32.5, 34, 'RideSync 7:2 [clip 1]', 24, 456},
    {false, 32.5, 0, 'Unrelated marker KEEP', 25, 789},
    {false, 12, 0, 'RideSync 7:2 [clip 1]', 26, 234},
  }
else
  error('unknown fixture scenario')
end
local next_id = 100
local undo_depth = 0
reaper = {}
function reaper.CountProjectMarkers(project)
  assert(project == 0)
  local markers, regions = 0, 0
  for _, entry in ipairs(entries) do
    if entry[1] then regions = regions + 1 else markers = markers + 1 end
  end
  return #entries, markers, regions
end
function reaper.EnumProjectMarkers3(project, index)
  assert(project == 0)
  local entry = entries[index + 1]
  if not entry then return 0 end
  return 1, table.unpack(entry)
end
function reaper.AddProjectMarker2(project, region, pos, finish, name, wanted, color)
  assert(project == 0 and undo_depth == 1)
  local id = wanted >= 0 and wanted or next_id
  next_id = next_id + 1
  for _, entry in ipairs(entries) do assert(entry[5] ~= id, 'must not replace existing IDs') end
  entries[#entries + 1] = {region, pos, finish, name, id, color}
  return id
end
function reaper.Undo_BeginBlock()
  assert(undo_depth == 0)
  undo_depth = 1
end
function reaper.Undo_EndBlock()
  assert(undo_depth == 1)
  undo_depth = 0
end
function reaper.UpdateArrange() assert(undo_depth == 0) end
io.write('phase\tkind\tid\tpos\tend\tname\tcolor\n')
for _, phase in ipairs({'first', 'repeat'}) do
  dofile(adapter)
  for _, entry in ipairs(entries) do
    io.write(string.format('%s\t%s\t%d\t%.9f\t%.9f\t%s\t%d\n', phase,
      entry[1] and 'region' or 'marker', entry[5], entry[2], entry[3], entry[4], entry[6]))
  end
end
