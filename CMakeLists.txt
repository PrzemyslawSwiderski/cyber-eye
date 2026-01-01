# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.20)

# add the component directories that we want to use
set(EXTRA_COMPONENT_DIRS

  # NOTE: if you add any custom components, you should add them to a
  #       `components/` folder within this repository and update the line below
  #       to point to it.
  #
  "components/gen_bmgr_codes"

  # NOTE: if we didn't use the idf-component-manager to get the espp
  #       dependencies, we could instead have ESPP be a submodule within our
  #       'components' folder, which would mean we'd need to add it to the DIRS:
  #
  # "components/espp/components/"
  #
  # NOTE: alternatively, we could install espp system-wide, so we could just
  #       point it to wherever we have espp installed.
  #
  # "/path/to/espp/components/"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(
  COMPONENTS
  # NOTE: you can add additional esp-idf and espp components you want to use to
  #       the line below.
  #
  # Alternatively, If they exist in the component registry, then you can ignore
  # adding them here, and instead use call
  #
  # `idf.py add-dependency "<namespace>/<component>^<version>"`
  #
  # e.g.
  #
  # `idf.py add-dependency "espp/task>=1.0"`
  "main esptool_py esp_http_server esp_driver_jpeg gen_bmgr_codes"
  CACHE STRING
  "List of components to include"
  )

project(cyber-eye)
# spiffs_create_partition_image(storage data FLASH_IN_PROJECT)

set(CMAKE_CXX_STANDARD 20)
