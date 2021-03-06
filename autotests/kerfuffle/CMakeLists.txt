set(RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

set(JSONINTERFACE_SOURCES
    jsonarchiveinterface.cpp
    jsonparser.cpp
)
add_library(jsoninterface STATIC ${JSONINTERFACE_SOURCES})
target_link_libraries(jsoninterface kerfuffle)

ecm_add_tests(
    addtoarchivetest.cpp
    archivetest.cpp
    createdialogtest.cpp
    metadatatest.cpp
    mimetypetest.cpp
    LINK_LIBRARIES kerfuffle Qt5::Test
    NAME_PREFIX kerfuffle-)

ecm_add_tests(
    jobstest.cpp
    LINK_LIBRARIES jsoninterface Qt5::Test
    NAME_PREFIX kerfuffle-)

# metadatatest needs the number of plugins actually installed by an install() command.
list(REMOVE_ITEM INSTALLED_KERFUFFLE_PLUGINS "")
list(LENGTH INSTALLED_KERFUFFLE_PLUGINS INSTALLED_COUNT)
target_compile_definitions(metadatatest PRIVATE -DPLUGINS_COUNT=${INSTALLED_COUNT})
