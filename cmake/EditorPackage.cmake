# The editor package: the programs with what they read at run time, the documentation and the test projects,
# ready to unpack and run (docs/editor/Пакет редактора.md). It is laid out by
#     cmake --install build --config Release --component EditorPackage --prefix <folder>
# A component of its own: the FetchContent dependencies have install rules too, and they stay out of the package.

# elm is next to editor: Run Standalone runs it, Build Game copies it
install(TARGETS editor elm RUNTIME DESTINATION . COMPONENT EditorPackage)

install(DIRECTORY shaders/ DESTINATION shaders COMPONENT EditorPackage)
install(DIRECTORY resources/ DESTINATION resources COMPONENT EditorPackage)

install(DIRECTORY docs/ DESTINATION docs COMPONENT EditorPackage
    PATTERN "CLAUDE.md" EXCLUDE
    PATTERN ".obsidian" EXCLUDE
    PATTERN ".trash" EXCLUDE
)

install(DIRECTORY projects/ DESTINATION projects COMPONENT EditorPackage
    PATTERN "Build" EXCLUDE
)

# The root .settings is a working file of the repository; this one makes the package folder the engine root
install(FILES cmake/EditorPackage.settings DESTINATION . RENAME .settings COMPONENT EditorPackage)

# The Visual C++ runtime next to the programs: a machine may not have the Redistributable.
# Build Game copies the .dll files next to elm into the game too.
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT EditorPackage)
    include(InstallRequiredSystemLibraries)
endif()
