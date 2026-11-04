cmake_minimum_required(VERSION 3.21)
project(tacit-portal LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core DBus Network)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_executable(tacit-portal src/main.cpp src/chooser.cpp src/chooser.h src/request.cpp src/request.h src/dialogbridge.cpp src/dialogbridge.h src/appchooser.cpp src/appchooser.h)
target_link_libraries(tacit-portal PRIVATE Qt6::Core Qt6::DBus Qt6::Network)

# stand-in for KDE's backend, used by tests/jail-test.sh
add_executable(fake-kde tests/fake-kde.cpp)
target_link_libraries(fake-kde PRIVATE Qt6::Core Qt6::DBus)

# test client, used by tests/run.sh
add_executable(portal-call tests/portal-call.cpp)
target_link_libraries(portal-call PRIVATE Qt6::Core Qt6::DBus)

# the bridge alone on a private socket, used by tests/bridge-test.py
add_executable(bridge-daemon tests/bridge-daemon.cpp src/dialogbridge.cpp src/dialogbridge.h)
target_link_libraries(bridge-daemon PRIVATE Qt6::Core Qt6::Network)

# stand-in for the GTK AppChooser backend, used by tests/open-with-test.py
add_executable(fake-gtk tests/fake-gtk.cpp)
target_link_libraries(fake-gtk PRIVATE Qt6::Core Qt6::DBus)
