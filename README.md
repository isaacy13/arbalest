# Arbalest

## Introduction

The project aims to create a geometry editor for BRL-CAD with the addition of a new tool for verification and validation. The project should ideally be an improvement over the existing editors MGED and Archer.

## Building

1. Install and configure Qt. Qt5.14.2 has been used for development of this project.
2. Clone BRL-CAD (`git clone https://github.com/BRL-CAD/brlcad.git`), build, and install it
3. Clone rt-cubed (`git clone https://github.com/BRL-CAD/rt-cubed.git`) and build by following its README. If you have done correctly you should be able to run the target QtGUI in rt-cubed (just for verification).
4. Go to the src directory of rt-cubed repository in terminal.
5. Clone this project into src. (`git clone https://github.com/BRL-CAD/arbalest.git`)
6. Add this project to rt-cubed's CMakeList file. (append "ADD_SUBDIRECTORY(./arbalest)" to the end of rt-cubed\src\CMakeLists.txt)
7. Build and run the target arbalest.

## Requirements

This code has been run and tested on:

- Windows 10
- Windows 11
- Ubuntu 22
- MacOS 12


## External Dependecies

- BRL-CAD - Download latest version at https://github.com/BRL-CAD/brlcad
- rt-cubed - Download latest version at https://github.com/BRL-CAD/rt-cubed
- Qt5.14.2 - Download at https://download.qt.io/archive/qt/5.14/5.14.2/
- Git - Download latest version at https://git-scm.com/book/en/v2/Getting-Started-Installing-Git
- GitHub Desktop (Not needed, but HELPFUL) at https://desktop.github.com/

## Installation

Part 1: Install Qt5.14.2 [(Installation Link)](https://download.qt.io/archive/qt/5.14/5.14.2/)

- For Windows: download .exe file and run the downloaded file 

- For MacOS: download .dmg file and run the downloaded file 

- For Linux:  

 - Download .run file 

 - Open a terminal window and move to the folder containing the download file. 

 - Use the command `chmod +x filename.run` to make the file executable. 

 - Use the command `./filename.run` to execute the .run file. 

 - If you receive a "permission denied" error after performing steps 2 or 3, you can try adding sudo to the beginning of the commands referenced in those steps (e.g. sudo ./filename.run). 

- Note the installation folder as it would be used in future steps. (Referenced as `PathToQtDir` in this guide) 

- In the Select Components menu, select the component associated with your compiler (e.g. MSVS 2017 64-bit). (Referenced as CompilerFolder in this guide) 

Part 2: Install BRL-CAD 

- Read through [README](https://github.com/BRL-CAD/brlcad/blob/main/README) from [BRL-CAD github repository](https://github.com/BRL-CAD) and follow this [guide](https://brlcad.org/wiki/Compiling) for installation instruction to Step 4: Create a Build directory. 

- Step 5: Configure 

 - It's first recommended during configure and compilation that you temporarily suspend any anti-virus software as they're notorious for issuing false-positives alerts and blocking necessary operations. Some BRL-CAD tools use temporary network ports that will need to be allowed as well, if prompted. 

 - Next, you'll run CMake to configure and generate a build system. 

 - CMake needs to know the top-level source dir (i.e., "brlcad" dir you cloned) and "build" dir you made are located. For your first time, we also recommend setting these CMake variables to avoid trivial issues: 
```
 BRLCAD_ENABLE_STRICT=OFF 

 BRLCAD_ENABLE_COMPILER_WARNINGS=OFF 

 BRLCAD_BUNDLED_LIBS=ON 
```
 - Since arbalest is a Qt-based software, you would need to include following CMake variables: 

```
   BRLCAD_ENABLE_QT=ON

   Qt5_DIR=PathToQtDir/CompilerFolder/lib/cmake/Qt5 
```

   (e.g. C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5) 

  - Windows: you can use CMake GUI where you'll specify 1) your source dir (i.e. path to the "brlcad" folder that has README), 2) the build dir, and 3) variables. CMake does not display variables the first time you run, but you can "+ Add Entry" to add them or run "Configure" to list them. Once dirs and variables are set, run "Configure" again and finally "Generate" to create a Visual Studio build system. 

  - Mac: You can either run CMake GUI like described for Windows, specifying your source dir, build dir, and the above variables, or you can follow the steps below for Linux in Terminal. 

  - Linux: 
```
  cd brlcad/build 

  cmake .. -DBRLCAD_ENABLE_STRICT=NO -DBRLCAD_BUNDLED_LIBS=ON -DCMAKE_BUILD_TYPE=Release -DBRLCAD_ENABLE_QT=ON -DQt5_DIR=/PathToQtDir/CompilerFolder/lib/cmake/Qt5 
```
    See [INSTALL](https://github.com/BRL-CAD/brlcad/blob/main/INSTALL) for more CMake options. 

    If CMake was successful, there will be a summary of the build printed at the end, it will say "Configured", and then "Generated" after a build system is finally done being written out for compiling BRL-CAD. 

    If CMake was unsuccessful, scan the output for any error messages indicating what is failing or missing as it's typically a missing tool, missing library, or erroneous setting. 

- Step 6: Compile 

 - Windows: navigate to your build directory and open the BRLCAD.sln file with Visual Studio. You can ignore any warning about a duplicate "regress" target. Compile the default ALL_BUILD solution. 

 - Mac, Linux: while still in the build directory from Step 4, run 
```
 make 
```
   If you have multiple cores, you can alternatively compile in parallel specifying a desire number of cores, e.g., 
```
 make -j10 
```
   If the build fails, re-run while capturing all output to a log:  
```
 make 2>&1 | tee build.log  
```
   Please [report](https://github.com/BRL-CAD/brlcad/issues) any build failures. 

   Note: Compilation can take anywhere from a couple minutes to an hour depending on hardware. If you had a quad-core CPU, you might run '"make -j4" to speed things up by compiling in parallel. 

- Step 7: Run! 

  You don't have to install to run BRL-CAD. You can run the binaries you just compiled as they are in the brlcad/build/bin directory. On Windows, they can be in Debug/ or Release/ folders. Try running these two binaries to make sure BRL-CAD and Qt libraries were installed correctly. 
```
bin/mged 

bin/qged 
```
Part 2: Install rt-cubed and arbalest 

- Read through [README](https://github.com/BRL-CAD/rt-cubed/blob/main/README) from rt-cubed github repository and README from arbalest github repository. 

- Step 1: Download rt-cubed 

  In the desired directory, open a Terminal window and run this: (Recommend the same directory as where BRL-CAD folder was installed) 
```
git clone https://github.com/BRL-CAD/rt-cubed 
```
- Step 2: Download arbalest 

  Go to the src directory of the rt-cubed repository (rt-cubed/src) in Terminal. 

  Clone the source code of our project into src by running this in terminal: 
```
git clone https://github.com/BRL-CAD/arbalest.git 
```
- Step 3: Add arbalest to CMakeList.txt 

  Open CMakeLists.txt in src directory of the rt-cubed repository (rt-cubed/src/CMakeLists.txt) and append 
```
ADD_SUBDIRECTORY(./arbalest) 
```
   to the end of the file.  

- Step 4: Create a Build directory 

  You can create a build folder anywhere, but it's typically named "build" and is in or near your "rt-cubed" source directory. 

 - Windows: create a folder named "build" inside your "rt-cubed" folder 

 - Mac, Linux: 
```
 mkdir -p rt-cubed/build 
```
- Step 5: Configure 

  You'll run CMake to configure and generate a build system. CMake needs to know the top-level source dir (i.e., "rt-cubed" dir you cloned) and "build" dir you made are located. The following variables also need to be set for Arbalest to run correctly. 
```
BRLCAD_BASE_DIR=PathToBRLCAD/BuildDir 
```
  (The path to the build directory you created in Part 2. Install BRL-CAD Step 4: Create a Build directory) 
```
BRLCAD_VERSION=7.32.6 
```
  (Please check [README](https://github.com/BRL-CAD/brlcad/blob/main/README) from [BRL-CAD github repository](https://github.com/BRL-CAD) to find out the version you cloned) 
```
Qt5_DIR=PathToQtDir/CompilerFolder/lib/cmake/Qt5 
```
  (This path should be the same as the one used previously in Part 2. Install BRL-CAD Step 5: Configure) 

 - Windows: you can use CMake GUI where you'll specify 1) your source dir (i.e. path to the "brlcad" folder that has README), 2) the build dir, and 3) variables. CMake does not display variables the first time you run, but you can "+ Add Entry" to add them or run "Configure" to list them. Once dirs and variables are set, run "Configure" again and finally "Generate" to create a Visual Studio build system. 

 - Mac: You can either run CMake GUI like described for Windows, specifying your source dir, build dir, and the above variables, or you can follow the steps below for Linux in Terminal. 

 - Linux: 
```
 cd rt-cubed/build 

 cmake .. -DCMAKE_BUILD_TYPE=debug -DBRLCAD_BASE_DIR=PathToBRLCAD/BuildDir -DBRLCAD_VERSION=7.32.6 -DQt5_DIR=/PathToQtDir/CompilerFolder/lib/cmake/Qt5 
```
- Step 6: Compile 

 - Windows: navigate to your build directory and open the rt_cubed.sln file with Visual Studio. Compile and build arbalest ONLY. 

 - Mac, Linux: while still in the build directory from Step 4, run: 
```
 make arbalest 
```
   If you have multiple cores, you can alternatively compile in parallel specifying a desire number of cores, e.g., 
```
 make arbalest -j10 
```
- Step 7: Run! 

  Navigate to the bin directory in your build directory and run this in Terminal: 
```
./arbalest 
```
  And our project should be running!

## Tests

Unit tests are implemented to test each parser within Arbalest. Currently, running `./doit.sh` will run these tests; however, file paths are hard coded and relative to the developers personal directories. If a user wishes to run these tests, they must edit the .sh file and add their own respective directories.

## Support

The support of this app has been officially closed. This project may be continued on further to add more features by a future capstone development team or by AFC Devcom.

## Extra Help

Please contact Pauline Wade (paulinewade@tamu.edu) for more information on CSCE 482 or Sean Morrison (brlcad@mac.com) for more information on this project.

Use mouse wheel to go forward backward.
