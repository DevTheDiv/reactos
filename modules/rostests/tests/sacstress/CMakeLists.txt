add_executable(sacstress sacstress.c)
set_module_type(sacstress win32cui)
add_importlibs(sacstress msvcrt kernel32)
add_rostests_file(TARGET sacstress)
