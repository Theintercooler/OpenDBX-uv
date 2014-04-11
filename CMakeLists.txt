cmake_minimum_required(VERSION 2.8)
project(odbxuv C)

find_package(PkgConfig)

if(${PKG_CONFIG_FOUND})
    message(STATUS "Using pkg-config to detect opendbx...")
    pkg_check_modules(OPENDBX opendbx)

    if(OPENDBX_FOUND)
        message(STATUS "Found Opendbx.")
        message(STATUS "include: ${OPENDBX_INCLUDE_DIRS}")
    else()
        message(WARNING "opendbx not found. No database support!")
    endif()
else()
    set(OPENDBX_LIBRARIES opendbx)
    find_path(OPENDBX_INCLUDE_DIRS odbx.h)
    find_library(OPENDBX_LIBRARY_DIRS ${OPENDBX_LIBRARIES})
endif()

include_directories(${OPENDBX_INCLUDE_DIRS})
link_directories(${OPENDBX_LIBRARY_DIRS})

option(HAVE_LOCAL_LIBUV 0)
if(${HAVE_LOCAL_LIBUV})
    add_subdirectory(libraries/uv)
endif()

set(ODBXUV_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENDBX_INCLUDE_DIRS}
    ${UV_INCLUDE_DIRS}
    CACHE INTERNAL "Odbxuv include directories")

set(ODBXUV_LIBRARY
    "odbxuv"
    CACHE INTERNAL "Odbxuv library name")

set(ODBXUV_LIBRARIES
    ${ODBXUV_LIBRARY}
    ${OPENDBX_LIBRARIES}
    CACHE INTERNAL "Odbxuv libraries")

set(ODBXUV_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db.c)

set(ODBXUV_MODE "STATIC")

include_directories(${ODBXUV_INCLUDE_DIRS})

add_library(${ODBXUV_LIBRARY} ${ODBXUV_MODE}
    ${ODBXUV_SOURCES})

set(ODBXUV_BUILD_TESTS 1)

if(${ODBXUV_BUILD_TESTS})
    add_executable(${ODBXUV_LIBRARY}_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/test/db_test.c)

    target_link_libraries(
        ${ODBXUV_LIBRARY}_tests
        ${ODBXUV_LIBRARIES}
        ${UV_LIBRARIES})

    if(NOT DEFINED INSTALL_RUNTIME_DIR)
        set(INSTALL_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
    endif()

    install(TARGETS
        ${ODBXUV_LIBRARY}_tests
        RUNTIME DESTINATION ${INSTALL_RUNTIME_DIR})
endif()

find_package(Doxygen)
if(DOXYGEN_FOUND)
    message("-- Adding doc target using doxygen.")

    if(NOT DEFINED INSTALL_DOC_DIR)
        set(INSTALL_DOC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/docs)
    endif()

    file(MAKE_DIRECTORY ${INSTALL_DOC_DIR})

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
        ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    add_custom_target(doc_odbxuv
        ${DOXYGEN_EXECUTABLE}
        ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen" VERBATIM)

    set(ODBXUV_HAVE_DOCS 1 CACHE INTERNAL "Have odbxuv documentation")
endif(DOXYGEN_FOUND)
