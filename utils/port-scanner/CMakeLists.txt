cmake_minimum_required(VERSION 3.10)
project(SerialPortScanner)

find_package(Boost REQUIRED COMPONENTS filesystem)

add_executable(serial_scanner main.cpp)
 
target_link_libraries(serial_scanner PRIVATE Boost::filesystem)