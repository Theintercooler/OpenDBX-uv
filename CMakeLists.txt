project(odbxuv C)

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

    install(TARGETS
        ${ODBXUV_LIBRARY}_tests
        RUNTIME DESTINATION ${INSTALL_RUNTIME_DIR})
endif()

find_package(Doxygen)
if(DOXYGEN_FOUND)
    message("-- Adding doc target using doxygen.")
    
    if(${INSTALL_DOC_DIR} EQUAL "")
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
