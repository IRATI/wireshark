          Explain the cmake build system for wireshark

   $Id$

                           Notice 

   To find out the current state of the cmake implementaion for
   Wireshark, please take a look at "What needs to be done?" below.
   Basically this is an experiment and if we find out that it works
   and we like cmake more than autofoo we might switch one day.

Table of contents
=================

How to get started with cmake?
Why cmake?
Why not cmake?
What needs to be done?
Links regarding cmake

How to get started with cmake?
==============================

You can find documentation on cmake at: http://www.cmake.org/

cmake is designed to support out of tree builds. So much so, that
in tree builds do not work properly in all cases.

How to do out of tree build (Unix/Linux):
1) Install cmake.
2) Assuming, you are in the top directory of the wireshark source
   cd ..
3) mkdir build
4) cd build
5) cmake ../<Name_of_WS_source_dir>
6) make
6) (as root) umask 0022 && make install

Note 1:
in step 5), you may override the defaults for features:
 cmake -DENABLE_CAP=OFF ../<Name_of_WS_source_dir>
 will disable the capabilities check.

Note 2:
 On OS X, you may want to run cmake like this:
 cmake -DENABLE_CAP=OFF -G "Unix Makefiles"

Note 3:
  After running cmake, you can always run "make help" to see
  a list of all possible make targets.

Note 4:
  Cmake honors user umask for creating directories as of now:
  http://public.kitware.com/Bug/view.php?id=9620
  To get predictable results please set umask explicitly.

Why cmake?
==========
- Can create project files for some MS and Apple IDEs.
- Fast
- Easier to understand/learn
- Doesn't create any files in the source tree in case
  of out of tree builds
- One build infrastructure even including Windows
...

Why not cmake?
==============
- Lots of work to do
- Everyone who wants to build from source needs cmake
- Current state of documentation isn't really better than
  autofoo documentation, in some respect it's even worse
  (you need to buy a book to get an explanation as to how
  cmake really works).
...

What works?
===========

All the executables now build from clean source on:
* 32bit openSUSE 11.3: (gnu)make and gcc
* 64bit FedoraXXX
* 32bit Ubuntu 9.04
* 32bit Ubuntu 10.04
* 64bit Debian Wheezy

What needs to be done?
======================

- Add asn1 autogen target (assigned: krj)
- Add back platform specific objects.
- Fix places in the cmake files marked as todo.
- Guides are not installed.
- Build source package (using CPack).
  This is obsolete if we decide to release VCS snapshots instead
- Build rpm package (using CPack).
- Build dpkg package (using CPack).
  This is obsolete, we should call CMake from debian/rules instead, using dh
  (rbalint)
- Add back checkAPI target.
- Test and add support for other platforms (BSDs, OSX,
  Solaris, Win32, Win64, ...)
...

Links regarding cmake
=====================
The home page of the cmake project
	http://www.cmake.org/

The home page of the cmake project documentation
	http://www.cmake.org/Wiki/CMake

About cmake in general and why KDE4 uses it
	http://lwn.net/Articles/188693/

Introductory/tutorial presentation
	http://ait.web.psi.ch/services/linux/hpc/hpc_user_cookbook/tools/cmake/docs/Cmake_VM_2007.pdf

Introductory article in Linux Journal
	http://www.linuxjournal.com/node/6700/print

Useful variables
	http://www.cmake.org/Wiki/CMake_Useful_Variables

cmake FAQ
	http://www.cmake.org/Wiki/CMake_FAQ

Additional cmake modules
	http://code.google.com/p/cmake-modules/
