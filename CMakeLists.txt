cmake_minimum_required(VERSION 3.16)

idf_component_register(
  SRCS
    "src/device-info.c"
    "src/hollows.c"
    "src/panel.c"
    "src/pixels.c"
    "src/task-ble.c"
    "src/task-io.c"
    "src/utils.c"
    "src/demo/background-pixies.c"
    "src/demo/panel-test.c"

  INCLUDE_DIRS
    "include"

  REQUIRES
    "firefly-ethers"
    "firefly-scene"

  PRIV_REQUIRES
    "bt"
    "efuse"
    "firefly-display"
    "nvs_flash"
)
