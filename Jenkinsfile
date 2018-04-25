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
            sh "cd depends && make -j${parallelismFactor} && cd .."
            sh "./autogen.sh"
            sh "CFLAGS=-fPIC CXXFLAGS=-fPIC CONFIG_SITE=${env.WORKSPACE}/depends/${linuxTriplet}/share/config.site ./configure --prefix=/ --with-gui=qt5 --with-pic"
            sh "make clean"
            sh "cd src && make obj/build.h && cd .."
            sh "make -j${parallelismFactor}"
            sh "make install DESTDIR=${env.WORKSPACE}/${linuxTriplet}-dist"
          }
        }
        stage("Build Windows x64") {
          steps {
            sh "cd depends && make HOST=${windowsTriplet} -j${parallelismFactor} && cd .."
            sh "./autogen.sh"
            sh "CONFIG_SITE=${env.WORKSPACE}/depends/${windowsTriplet}/share/config.site ./configure --prefix=/ --with-gui=qt5"
            sh "make clean"
            sh "cd src && make obj/build.h && cd .."
            sh "make -j${parallelismFactor}"
            sh "make deploy"
            sh "make install DESTDIR=${env.WORKSPACE}/${windowsTriplet}-dist"
            sh "cp *.exe ./dist/"
          }
        }
      }
    }
  }
}
