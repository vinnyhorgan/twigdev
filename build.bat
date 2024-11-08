@echo off
gcc main.c system_win32.c -o twig.exe -Os -s -lgdi32
