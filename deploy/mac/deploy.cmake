# SPDX-FileCopyrightText: (C) 2024 Chris Rizzitello <sithlord48@gmail.com>
# SPDX-License-Identifier: MIT

# HACK This is set when the files is included so its the real path
# calling CMAKE_CURRENT_LIST_DIR after include would return the wrong scope var
set(MY_DIR ${CMAKE_CURRENT_LIST_DIR})
set(OSX_BUNDLE ${BUILD_OSX_BUNDLE})

set(OS_STRING "macos-${BUILD_ARCHITECTURE}")

if (OSX_BUNDLE)
  set(MACDEPLOYQT_LIBPATH_ARGS)
  foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
    if(EXISTS "${_prefix}/lib")
      list(APPEND MACDEPLOYQT_LIBPATH_ARGS "-libpath=${_prefix}/lib")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES MACDEPLOYQT_LIBPATH_ARGS)

  set(MACDEPLOYQT_LIBPATH_ARGS_CODE)
  foreach(_arg IN LISTS MACDEPLOYQT_LIBPATH_ARGS)
    string(APPEND MACDEPLOYQT_LIBPATH_ARGS_CODE "\n      \"${_arg}\"")
  endforeach()

  if(APPLE_CODESIGN_DEV)
    set(MACDEPLOYQT_CODESIGN_ARGS -timestamp "-codesign=${APPLE_CODESIGN_DEV}")
  else()
    set(MACDEPLOYQT_CODESIGN_ARGS -timestamp -codesign=-)
  endif()

  set(MACDEPLOYQT_CODESIGN_ARGS_CODE)
  foreach(_arg IN LISTS MACDEPLOYQT_CODESIGN_ARGS)
    string(APPEND MACDEPLOYQT_CODESIGN_ARGS_CODE "\n      \"${_arg}\"")
  endforeach()

  install(CODE "
    set(_macdeployqt_app \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\")
    set(_macdeployqt_args
      ${DEPLOYQT}
      \"\${_macdeployqt_app}\"
      \"-executable=\${_macdeployqt_app}/Contents/MacOS/${CMAKE_PROJECT_NAME}-core\"
      ${MACDEPLOYQT_LIBPATH_ARGS_CODE}
      -always-overwrite
    )

    # The first pass deploys plugins. A second pass is needed for modular Qt
    # installs where those plugins introduce additional framework dependencies.
    execute_process(
      COMMAND \${_macdeployqt_args} -no-codesign
      RESULT_VARIABLE _macdeployqt_result
    )

    if(NOT _macdeployqt_result EQUAL 0)
      message(FATAL_ERROR \"macdeployqt failed: \${_macdeployqt_result}\")
    endif()

    execute_process(
      COMMAND \${_macdeployqt_args}
      ${MACDEPLOYQT_CODESIGN_ARGS_CODE}
      RESULT_VARIABLE _macdeployqt_result
    )

    if(NOT _macdeployqt_result EQUAL 0)
      message(FATAL_ERROR \"macdeployqt failed: \${_macdeployqt_result}\")
    endif()
  ")

  set(MACOS_APP_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/macos-app-install" CACHE PATH
    "Staging prefix used by the install-macos-app target.")
  set(MACOS_APP_INSTALL_DESTINATION "/Applications" CACHE PATH
    "Directory where the install-macos-app target installs the app bundle.")

  add_custom_target(install-macos-app
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${MACOS_APP_INSTALL_PREFIX}"
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --prefix "${MACOS_APP_INSTALL_PREFIX}"
    COMMAND "${MY_DIR}/install_app.sh"
            "${MACOS_APP_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app"
            "${MACOS_APP_INSTALL_DESTINATION}/${CMAKE_PROJECT_PROPER_NAME}.app"
    DEPENDS ${CMAKE_PROJECT_PROPER_NAME} ${CMAKE_PROJECT_NAME}-core app_translations
    COMMENT "Installing ${CMAKE_PROJECT_PROPER_NAME}.app to ${MACOS_APP_INSTALL_DESTINATION}"
    VERBATIM
  )

  set(CPACK_PACKAGE_ICON "${MY_DIR}/dmg-volume.icns")
  set(CPACK_DMG_BACKGROUND_IMAGE "${MY_DIR}/dmg-background.tiff")
  set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${MY_DIR}/generate_ds_store.applescript")
  set(CPACK_DMG_VOLUME_NAME "${CMAKE_PROJECT_PROPER_NAME}")
  set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE ON)
  set(CPACK_GENERATOR "DragNDrop")
endif()
