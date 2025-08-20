Import("env")
env.Append(CXXFLAGS=["-Wno-volatile", "-fpermissive"])

board_config = env.BoardConfig()
# array of VID:PID pairs
board_config.update("build.hwids", [
  ["0x303A", "0x82E5"]
])