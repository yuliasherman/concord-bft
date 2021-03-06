project (libbftcommunication LANGUAGES CXX)

set(bftcommunication_src
  src/CommFactory.cpp
  src/PlainUDPCommunication.cpp
)

if(${BUILD_COMM_TCP_PLAIN})
    set(bftcommunication_src ${bftcommunication_src} src/PlainTcpCommunication.cpp)
endif()
if(${BUILD_COMM_TCP_TLS})
    set(bftcommunication_src ${bftcommunication_src} src/TlsTCPCommunication.cpp)
endif()

add_library(bftcommunication ${bftcommunication_src})
target_include_directories(bftcommunication PUBLIC include)
target_link_libraries(bftcommunication PUBLIC util)

if(${BUILD_COMM_TCP_PLAIN} OR ${BUILD_COMM_TCP_TLS})
    set(Boost_USE_STATIC_LIBS ON) # only find static libs
    set(Boost_USE_MULTITHREADED ON)
    set(Boost_USE_STATIC_RUNTIME OFF)
    find_package(Boost ${MIN_BOOST_VERSION} COMPONENTS system filesystem REQUIRED)
    target_include_directories(bftcommunication PUBLIC ${Boost_INCLUDE_DIRS})
    target_link_libraries(bftcommunication PUBLIC ${Boost_LIBRARIES})
    if(${BUILD_COMM_TCP_PLAIN})
        target_compile_definitions(bftcommunication PUBLIC USE_COMM_PLAIN_TCP)
    elseif(${BUILD_COMM_TCP_TLS})
        find_package(OpenSSL REQUIRED)
        target_include_directories(bftcommunication PUBLIC ${OPENSSL_INCLUDE_DIR})
        target_link_libraries(bftcommunication PUBLIC ${OPENSSL_LIBRARIES})
        target_compile_definitions(bftcommunication PUBLIC USE_COMM_TLS_TCP)
    endif()
endif()
