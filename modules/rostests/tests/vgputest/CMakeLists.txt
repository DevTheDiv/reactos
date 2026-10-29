add_executable(vgputest vgputest.c)
set_module_type(vgputest win32cui)
add_importlibs(vgputest msvcrt kernel32)
add_rostests_file(TARGET vgputest)
