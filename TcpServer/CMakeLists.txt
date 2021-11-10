cmake_minimum_required(VERSION 3.10)

# set the project name
project(TcpServer)

# add the executable
add_executable(TcpServer tcpserv.cpp)
target_link_libraries(TcpServer -static)

if(${WIN32})
add_definitions(-DWIN)
target_link_libraries(TcpServer ws2_32)
endif()
