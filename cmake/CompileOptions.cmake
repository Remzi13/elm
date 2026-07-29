# C++23 strict warnings and standard flags

function(target_set_warnings TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:preprocessor
            /Zc:enumTypes
            /Zc:templateScope
        )
    else()
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Wnon-virtual-dtor
            -Wunused
            -Woverloaded-virtual
        )
    endif()
endfunction()
