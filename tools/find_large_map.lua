-- Test helper for a user-owned FE8U ROM/save. It advances ordinary input until
-- a tactical map larger than 15x10 is observed, then captures a disposable
-- mGBA state and screenshot under /tmp. It never writes emulated memory.
local captured = false

local function valid_ewram_pointer(address)
  return address >= 0x02000000 and address < 0x02040000
end

local function on_frame()
  local frame = emu:currentFrame()
  local width = emu:read16(0x0202E4D4)
  local height = emu:read16(0x0202E4D6)
  local rows = emu:read32(0x0859A9D4)

  if not captured and width > 0 and width <= 64 and height > 0 and height <= 64
      and (width > 15 or height > 10) and valid_ewram_pointer(rows) then
    captured = true
    emu:setKeys(0)
    console:log(string.format("FE8 large map found at frame %d: %dx%d", frame, width, height))
    emu:saveStateFile("/tmp/fe8-large-map.ss")
    emu:screenshot("/tmp/fe8-large-map.png")
    return
  end

  -- Bit 0=A and bit 3=Start. These one-frame pulses traverse the health
  -- warning, skip the intro, choose Continue, and choose the default save.
  local input = {
    [90] = 1,
    [180] = 8,
    [270] = 8,
    [360] = 1,
    [450] = 1,
    [540] = 1,
  }
  emu:setKeys(input[frame] or 0)

  if frame == 720 then
    emu:screenshot("/tmp/fe8-navigation-check.png")
  end
end

callbacks:add("frame", on_frame)
