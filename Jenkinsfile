def parallelismFactor = 2
def linuxTriplet = "x86_64-pc-linux-gnu"
def windowsTriplet = "x86_64-w64-mingw32"

pipeline {
  agent any

  stages {
    stage("Checkout") {
      steps {
        checkout scm
      }
    }

    stage("Build Merit Core") {
      parallel {
        stage("Build Linux x64") {
          steps {
              sh cmd: "cd depends && make -j${parallelismFactor} && cd ..", name: "Build dependencies"
              sh cmd: "./autogen.sh", name: "Execute autogen script"
              sh cmd: "CFLAGS=-fPIC CXXFLAGS=-fPIC CONFIG_SITE=$PWD/depends/${linuxTriplet}/share/config.site ./configure --prefix=/ --with-gui=qt5 --with-pic", name: "Configure"
              sh cmd: "cd src && make obj/build.h && cd ..", name: "Create `build.h`"
              sh cmd: "make -j${parallelismFactor}", name: "Build"
              sh cmd: "make install DESTDIR=$PWD/${linuxTriplet}-dist", name: "Install"
            }
          }
        }
        stage("Build Windows x64") {
          steps {
              sh cmd: "cd depends && make HOST=${windowsTriplet} -j${parallelismFactor} && cd ..", name: "Build dependencies"
              sh cmd: "./autogen.sh", name: "Execute autogen script"
              sh cmd: "CONFIG_SITE=$PWD/depends/${windowsTriplet}/share/config.site ./configure --prefix=/ --with-gui=qt5", name: "Configure"
              sh cmd: "cd src && make obj/build.h && cd ..", name: "Create `build.h`"
              sh cmd: "make -j${parallelismFactor}", name: "Build"
              sh cmd: "make deploy", name: "Deploy"
              sh cmd: "make install DESTDIR=$PWD/${windowsTriplet}-dist", name: "Install"
              sh cmd: "cp *.exe ./dist/", name: "Copy binaries to deploy folder"
            }
          }
        }
      }
    }
  }
}
