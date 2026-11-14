#include <jni.h>
#include <android/log.h>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unistd.h>
#include "jfg_xr.h"
extern int main(int,char**);
extern "C" JNIEXPORT void JNICALL
Java_org_dexr_jfgxr_MainActivity_nativePrepare(JNIEnv *env,jclass,jobject activity,jstring folder) {
    JavaVM *vm=nullptr;env->GetJavaVM(&vm);
    jfgxr::setAndroidContext(vm,env->NewGlobalRef(activity));
    const char *path=env->GetStringUTFChars(folder,nullptr);
    setenv("APP_FOLDER_PATH",path,1);env->ReleaseStringUTFChars(folder,path);
    // Preserve engine diagnostics in logcat without creating a desktop console.
    int descriptors[2];
    if(pipe(descriptors)==0) {
        dup2(descriptors[1],STDOUT_FILENO);dup2(descriptors[1],STDERR_FILENO);close(descriptors[1]);
        setvbuf(stdout,nullptr,_IOLBF,0);setvbuf(stderr,nullptr,_IONBF,0);
        std::thread([fd=descriptors[0]] {
            FILE *stream=fdopen(fd,"r");char line[4096];
            while(fgets(line,sizeof(line),stream))__android_log_write(ANDROID_LOG_INFO,"JFGXR",line);
            fclose(stream);
        }).detach();
    }
}
extern "C" JNIEXPORT void JNICALL
Java_org_dexr_jfgxr_MainActivity_nativeExit(JNIEnv*,jclass) {jfgxr::requestExit();}
extern "C" __attribute__((visibility("default"))) int SDL_main(int argc,char **argv) {return main(argc,argv);}
