cmake_minimum_required(VERSION 3.20)
project(VozLargaPC VERSION 1.0.0 LANGUAGES CXX RC)

add_executable(VozLarga WIN32 main.cpp resources.rc)
target_compile_features(VozLarga PRIVATE cxx_std_17)
target_compile_definitions(VozLarga PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_link_libraries(VozLarga PRIVATE comctl32 comdlg32 shell32 gdi32 ole32 uuid)

if(MINGW)
  target_link_options(VozLarga PRIVATE -municode -Wl,--subsystem,windows)
elseif(MSVC)
  target_link_options(VozLarga PRIVATE /ENTRY:wWinMainCRTStartup)
endif()
