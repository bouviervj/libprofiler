libprofiler
===========

Realtime profiler for C/C++ applications

This project aims at dynamically profile Linux applications. It uses ncurses to display information in real time. You have to compile your program with -finstrument-functions from gcc to instrument entry and exit of each compiled function for your application, or a sub part of your code (library etc...). When loaded, it display dynamically, CPU functions consuming, computes most consuming call paths. It can also draw graphs against calls distributions for each functions.

