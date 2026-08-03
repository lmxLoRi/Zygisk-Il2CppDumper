#include <cstring>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include "hack.h"
#include "zygisk.hpp"
#include "log.h"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        auto package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        auto app_data_dir = env->GetStringUTFChars(args->app_data_dir, nullptr);
        preSpecialize(package_name, app_data_dir);
        env->ReleaseStringUTFChars(args->nice_name, package_name);
        env->ReleaseStringUTFChars(args->app_data_dir, app_data_dir);
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (enable_hack) {
            std::thread hack_thread(hack_prepare, game_data_dir, data, length);
            hack_thread.detach();
        }
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
    bool enable_hack = false;
    char *game_data_dir = nullptr;
    void *data = nullptr;
    size_t length = 0;

    void preSpecialize(const char *package_name, const char *app_data_dir) {
        bool match = false;
        int modfd = api->getModuleDir();
        if (modfd >= 0) {
            int fd = openat(modfd, "target.txt", O_RDONLY);
            if (fd >= 0) {
                char buf[4096];
                ssize_t n = read(fd, buf, sizeof(buf)-1);
                if (n > 0) {
                    buf[n] = 0;
                    char *line = strtok(buf, "\n\r");
                    while (line) {
                        while (*line == ' ' || *line == '\t') line++;
                        size_t len = strlen(line);
                        while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\r')) line[--len] = 0;
                        if (len > 0 && strcmp(package_name, line) == 0) { match = true; break; }
                        line = strtok(nullptr, "\n\r");
                    }
                }
                close(fd);
            }
        }
        if (!match) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        enable_hack = true;
        game_data_dir = new char[strlen(app_data_dir) + 1];
        strcpy(game_data_dir, app_data_dir);

#if defined(__i386__)
        auto path = "zygisk/armeabi-v7a.so";
#endif
#if defined(__x86_64__)
        auto path = "zygisk/arm64-v8a.so";
#endif
#if defined(__i386__) || defined(__x86_64__)
        int dirfd = api->getModuleDir();
        int fd = openat(dirfd, path, O_RDONLY);
        if (fd != -1) {
            struct stat sb{};
            fstat(fd, &sb);
            length = sb.st_size;
            data = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
            close(fd);
        } else {
            LOGW("Unable to open arm file");
        }
#endif
    }
};

REGISTER_ZYGISK_MODULE(MyModule)
