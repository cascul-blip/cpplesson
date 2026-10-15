cmake_minimum_required(VERSION 3.21)
project(LessonPlanner VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wextra)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets PrintSupport Test)

# SQLite: the system library where there is one (Linux), otherwise the
# amalgamation built into the app (Windows).
find_package(SQLite3 QUIET)
if(TARGET SQLite3::SQLite3)
    set(sqlite_target SQLite3::SQLite3)
elseif(TARGET SQLite::SQLite3)  # the name before CMake 4.3
    set(sqlite_target SQLite::SQLite3)
else()
    include(FetchContent)
    FetchContent_Declare(sqlite_amalgamation
        URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
        URL_HASH SHA256=77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784)
    FetchContent_MakeAvailable(sqlite_amalgamation)
    add_library(sqlite3_bundled STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3_bundled PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
    set(sqlite_target sqlite3_bundled)
endif()

# Everything that does not need a window; the tests link against this alone.
add_library(core STATIC
    src/core/backup.cpp
    src/core/config.cpp
    src/core/database.cpp
    src/core/fileid.cpp
    src/core/lock.cpp
    src/core/render.cpp
    src/core/schedule.cpp
    src/core/schoolcal.cpp
)
target_include_directories(core PUBLIC src)
target_link_libraries(core PUBLIC Qt6::Core ${sqlite_target})

# The window and everything in it, apart from main().
add_library(gui STATIC
    src/gui/cell_widget.cpp
    src/gui/export.cpp
    src/gui/main_window.cpp
    src/gui/options_dialog.cpp
    src/gui/theme.cpp
    src/gui/week_view.cpp
)
target_link_libraries(gui PUBLIC core Qt6::Widgets Qt6::PrintSupport)

set(app_sources src/main.cpp resources/resources.qrc)
if(WIN32)
    list(APPEND app_sources packaging/app.rc)
endif()
add_executable(LessonPlanner WIN32 ${app_sources})
target_compile_definitions(LessonPlanner PRIVATE APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(LessonPlanner PRIVATE gui)
if(NOT WIN32)
    set_target_properties(LessonPlanner PROPERTIES OUTPUT_NAME lessonplanner)
endif()

install(TARGETS LessonPlanner RUNTIME DESTINATION .)
if(WIN32)
    # the MSVC runtime DLLs, so the school computer needs nothing installed
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    include(InstallRequiredSystemLibraries)
endif()

enable_testing()
foreach(name schoolcal schedule lock_backup)
    add_executable(test_${name} tests/test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE core Qt6::Test)
    add_test(NAME ${name} COMMAND test_${name})
endforeach()

# drives the real window, without needing a screen
add_executable(test_gui tests/test_gui.cpp)
target_link_libraries(test_gui PRIVATE gui Qt6::Test)
add_test(NAME gui COMMAND test_gui)
set_tests_properties(gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
