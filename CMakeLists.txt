cmake_minimum_required(VERSION 3.24)

project( colorrace )

if ( ${CMAKE_SYSTEM_NAME} MATCHES "Darwin" )
set(MACOSX TRUE)
find_package(OpenSSL)
else()
endif()

set( SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src )

set(CMAKE_BUILD_TYPE Debug)

file( GLOB SOURCES ${SOURCE_DIR}/*.cpp ${SOURCE_DIR}/*.c )

add_executable( colorrace ${SOURCES} )

target_include_directories( colorrace PUBLIC crankshaft/include )

target_link_directories( colorrace PUBLIC crankshaft/lib )

if( MACOSX )
if( OPENSSL_FOUND ) 
target_include_directories( webmud PUBLIC ${OPENSSL_INCLUDE_DIR} )
target_link_libraries( webmud ${OPENSSL_LIBRARIES} )
target_link_libraries( webmud -lcrankshaft )
endif()
else()
target_link_libraries( colorrace m ssl crypto pthread m z dl crankshaft )
endif()



