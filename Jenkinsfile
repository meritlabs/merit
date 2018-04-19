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
            stage("Prebuild Dependencies") {
              steps {
                sh "cd depends && make -j${parallelismFactor} && cd .."
              }
            }
            stage("Configuration") {
              steps {
                sh "./autogen.sh"
                sh "CFLAGS=-fPIC CXXFLAGS=-fPIC CONFIG_SITE=$PWD/depends/${linuxTriplet}/share/config.site ./configure --prefix=/ --with-gui=qt5 --with-pic"
              }
            }
            stage("Build") {
              steps {
                sh "cd src && make obj/build.h && cd .."
                sh "make -j${parallelismFactor}"
              }
            }
            stage("Deploy") {
              steps {
                sh "make install DESTDIR=$PWD/${linuxTriplet}-dist"
              }
            }
          }
        }
        stage("Build Windows x64") {
          steps {
            stage("Prebuild Dependencies") {
              steps {
                sh "cd depends && make HOST=${windowsTriplet} -j${parallelismFactor} && cd .."
              }
            }
            stage("Configuration") {
              steps {
                sh "./autogen.sh"
                sh "CONFIG_SITE=$PWD/depends/${windowsTriplet}/share/config.site ./configure --prefix=/ --with-gui=qt5"
              }
            }
            stage("Build") {
              steps {
                sh "cd src && make obj/build.h && cd .."
                sh "make -j${parallelismFactor}"
              }
            }
            stage("Deploy") {
              steps {
                sh "make deploy"
                sh "make install DESTDIR=$PWD/${windowsTriplet}-dist"
                sh "cp *.exe ./dist/"
              }
            }
          }
        }
      }
    }
  }
}
