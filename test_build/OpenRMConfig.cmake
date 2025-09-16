
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was OpenRMConfig.cmake.in.nocuda                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

include("/usr/local/lib/cmake/openrm/OpenRMTarget.cmake")

set(
    OpenRM_INCLUDE_DIRS
        "/usr/local/include"
        "/usr/local/include/openrm"
)

set(
    OpenRM_LIBS
        openrm::openrm_attack
        openrm::openrm_kalman
        openrm::openrm_pointer
        openrm::openrm_solver
        openrm::openrm_video

        openrm::openrm_delay
        openrm::openrm_print
        openrm::openrm_serial
        openrm::openrm_tf
        openrm::openrm_timer

        openrm::openrm_uniterm
)

