/* Host doubles ONLY. Never linked into PS5 builds. */
#include "pkg_portal.hpp"
#include <cstdlib>
#include <unistd.h>
namespace hyperdeck::pkg {
std::vector<std::string> roots(){const char *root=std::getenv("HYPERDECK_PKG_TEST_ROOT");return root?std::vector<std::string>{root}:std::vector<std::string>{};}
std::vector<Volume> volumes(){return {{"HOST TEST / NOT A PS5","/host-test","internal",100000000000ULL,40000000000ULL,59000000000ULL}};}
bool prepare_helper(){return true;}
bool helper_available(){return !std::getenv("HYPERDECK_TEST_NO_HELPER");}
void reap(){}
int launch(const std::string &path){int fd[2];if(pipe(fd))return -1;const auto text="HOST TEST ONLY: argv[0]=hyperhen-pkg-install.elf argv[1]="+path+"\nHYPERDECK_PKG phase=install result=0x00000000\n";write(fd[1],text.data(),text.size());close(fd[1]);return fd[0];}
}
