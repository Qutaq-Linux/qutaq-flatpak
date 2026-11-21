cmake_minimum_required(VERSION 3.16)
project(qutaq-flatpak VERSION 1.0 LANGUAGES CXX)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets LinguistTools)
add_executable(qutaq-flatpak main.cpp localization.cpp)
target_compile_features(qutaq-flatpak PRIVATE cxx_std_17)
target_link_libraries(qutaq-flatpak PRIVATE Qt6::Widgets)
file(GLOB translation_catalogs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/translations/qutaq-flatpak_*.ts"
)
qt_add_translations(qutaq-flatpak
    TS_FILES ${translation_catalogs}
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -locations none
)
install(TARGETS qutaq-flatpak RUNTIME DESTINATION bin)
install(FILES qutaq-flatpak.desktop DESTINATION share/applications)
