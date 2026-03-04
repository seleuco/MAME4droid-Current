/*
 * This file is part of MAME4droid.
 *
 * Copyright (C) 2011 David Valdeita (Seleuco)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * In addition, as a special exception, Seleuco
 * gives permission to link the code of this program with
 * the MAME library (or with modified versions of MAME that use the
 * same license as MAME), and distribute linked combinations including
 * the two.  You must obey the GNU General Public License in all
 * respects for all of the code used other than MAME.  If you modify
 * this file, you may extend this exception to your version of the
 * file, but you are not obligated to do so.  If you do not wish to
 * do so, delete this exception statement from your version.
 */

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <android/log.h>

#include <math.h>

#include <pthread.h>

#include "com_seleuco_mame4droid_Emulator.h"

#define DEBUG 1

//mame4droid funtions
int  (*android_main)(int argc, char **argv) = NULL;
void (*setAudioCallbacks)(void *func1,void *func2,void *func3)= NULL;
void (*setVideoCallbacks)(void *func1,void *func2) = NULL;
void (*setInputCallbacks)(void *func1) = NULL;
void (*setDigitalData)(int i, unsigned long digital_status) = NULL;
void (*initMyOSD)(const char *path, int nativeWidth, int nativeHeight) = NULL;

void  (*setMyValue)(int key,int i, int value)=NULL;
int  (*getMyValue)(int key, int i)=NULL;
void  (*setMyValueStr)(int key, int i,const char *value)=NULL;
char *(*getMyValueStr)(int key,int i)=NULL;

void  (*setAnalogData)(int i, float v1,float v2)=NULL;

void (*setSAFCallbacks)(void *func1,void *func2,void *func3,void *func4) = NULL;

int  (*setKeyData)(int keyCode, int keyAction, char keyChar)=NULL;
int  (*setMouseData)(int i, int mouseAction, int button, float x, float y)=NULL;
int  (*setTouchData)(int i, int touchAction, float x, float y)=NULL;

void (*onSurfaceCreated)(void) = NULL;
void (*onDrawFrame)(void) = NULL;
void (*onChooseRenderer)(int) = NULL;

void (*getShaders)(int, const char***, int*) = NULL;
bool (*setShader)(const char*) = NULL;
void (*loadShaders)(const char*) = NULL;

/* Callbacks to Android */
jmethodID android_dumpVideo;
jmethodID android_changeVideo;
jmethodID android_openAudio;
jmethodID android_dumpAudio;
jmethodID android_closeAudio;
jmethodID android_initInput;
jmethodID android_safOpenFile;
jmethodID android_safReadDir;
jmethodID android_safGetNextDirEntry;
jmethodID android_safCloseDir;

static JavaVM *jVM = NULL;
static void *libdl = NULL;
static jclass cEmulator = NULL;

static jbyteArray jbaAudioBuffer = NULL;

static jobject audioBuffer=NULL;
static unsigned char audioByteBuffer[882 * 2 * 2 * 10];

static pthread_t main_tid;

static void load_lib(const char *str)
{
    char str2[256];

    memset(str2,0,sizeof(str2));
    strcpy(str2,str);
    strcpy(str2+strlen(str),"/libMAME4droid.so");

//#ifdef DEBUG
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "Attempting to load %s\n", str2);
//#endif

    if(libdl!=NULL)
        return;

    libdl = dlopen(str2, RTLD_NOW);
    if(!libdl)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Unable to load libMAME4droid.so: %s\n", dlerror());
        return;
    }

    android_main = dlsym(libdl, "myosd_droid_main");
     __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni","myosd_droid_main %d\n", android_main!=NULL);

    setVideoCallbacks = dlsym(libdl, "myosd_droid_setVideoCallbacks");
     __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni","myosd_droid_setVideoCallbacks %d\n", setVideoCallbacks!=NULL);

    setAudioCallbacks = dlsym(libdl, "myosd_droid_setAudioCallbacks");
     __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni","myosd_droid_setAudioCallbacks %d\n", setAudioCallbacks!=NULL);

    setInputCallbacks = dlsym(libdl, "myosd_droid_setInputCallbacks");
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni","myosd_droid_setInputCallbacks %d\n", setInputCallbacks!=NULL);

    setDigitalData = dlsym(libdl, "myosd_droid_setDigitalData");
     __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni","myosd_droid_setDigitalData %d\n", setDigitalData!=NULL);

    initMyOSD = dlsym(libdl, "myosd_droid_initMyOSD");
     __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni","myosd_droid_iinitMyOSD %d\n", initMyOSD!=NULL);

    setMyValue = dlsym(libdl, "myosd_droid_setMyValue");
     __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni","myosd_droid_setMyValue %d\n",setMyValue!=NULL);

    getMyValue = dlsym(libdl, "myosd_droid_getMyValue");
     __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni","myosd_droid_getMyValue %d\n", getMyValue!=NULL);

    setMyValueStr = dlsym(libdl, "myosd_droid_setMyValueStr");
     __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni","myosd_droid_setMyValueStr %d\n",setMyValueStr!=NULL);

    getMyValueStr = dlsym(libdl, "myosd_droid_getMyValueStr");
     __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni","myosd_droid_getMyValueStr %d\n", getMyValueStr!=NULL);

    setAnalogData = dlsym(libdl, "myosd_droid_setAnalogData");
     __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni","myosd_droid_setAnalogData %d\n", setAnalogData!=NULL);

    setSAFCallbacks = dlsym(libdl, "myosd_droid_setSAFCallbacks");
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni","setSAFCallbacks %d\n", setSAFCallbacks!=NULL);

    setKeyData = dlsym(libdl, "myosd_droid_setKeyData");
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "myosd_droid_setKeyData %d\n", setKeyData != NULL);

    setMouseData = dlsym(libdl, "myosd_droid_setMouseData");
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "myosd_droid_setMouseData %d\n", setMouseData != NULL);

    setTouchData = dlsym(libdl, "myosd_droid_setTouchData");
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "myosd_droid_setTouchData %d\n", setTouchData != NULL);

    onSurfaceCreated = dlsym(libdl, "myosd_video_onSurfaceCreated");
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "myosd_video_onSurfaceCreated %d\n", onSurfaceCreated != NULL);

    onDrawFrame = dlsym(libdl, "myosd_video_onDrawFrame");
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "myosd_video_onDrawFrame %d\n", onDrawFrame != NULL);

    onChooseRenderer = dlsym(libdl, "myosd_video_onChooseRenderer");
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "myosd_video_onChooseRenderer %d\n", onChooseRenderer != NULL);

    setShader = dlsym(libdl, "myosd_video_setShader");
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "myosd_video_setShader %d\n", setShader != NULL);

    getShaders = dlsym(libdl, "myosd_video_getShaders");
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "myosd_video_getShaders %d\n", getShaders != NULL);

    loadShaders = dlsym(libdl, "myosd_video_loadShaders");
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "myosd_video_loadShaders %d\n", loadShaders != NULL);
}

void myJNI_dumpVideo()
{
    JNIEnv *env;
    (*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4);

#ifdef DEBUG
    //__android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "dumpVideo");
#endif

   (*env)->CallStaticVoidMethod(env, cEmulator, android_dumpVideo);
}

void myJNI_changeVideo(int newWidth, int newHeight, int newVisWidth, int newVisHeight)
{
    JNIEnv *env;
    (*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4);

#ifdef DEBUG
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "changeVideo");
#endif

    (*env)->CallStaticVoidMethod(env, cEmulator, android_changeVideo, (jint)newWidth,(jint)newHeight,(jint)newVisWidth,(jint)newVisHeight );
}

void myJNI_closeAudio()
{
    JNIEnv *env;
    (*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4);

#ifdef DEBUG
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "closeAudio");
#endif

    (*env)->CallStaticVoidMethod(env, cEmulator, android_closeAudio);
}

void myJNI_openAudio(int rate, int stereo)
{
    JNIEnv *env;
    jobject tmp;
    (*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4);

#ifdef DEBUG
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "openAudio");
#endif

    (*env)->CallStaticVoidMethod(env, cEmulator, android_openAudio, (jint)rate,(jboolean)stereo);

    if(jbaAudioBuffer==NULL)
    {
        jbaAudioBuffer=(*env)->NewByteArray(env, 882*2*2*10);
        tmp = jbaAudioBuffer;
        jbaAudioBuffer=(jbyteArray)(*env)->NewGlobalRef(env, jbaAudioBuffer);
        (*env)->DeleteLocalRef(env, tmp);
    }
}

void myJNI_dumpAudio(void *buffer, int size)
{
    JNIEnv *env;
    (*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4);
    int attached = 0;

    if(env==NULL)
    {
        attached  = 1;
        (*jVM)->AttachCurrentThread(jVM,(void *) &env, NULL);
    }

#ifdef DEBUG
    //__android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "dumpAudio %ld %d",buffer, size);
#endif

    (*env)->SetByteArrayRegion(env, jbaAudioBuffer, 0, size, (jbyte *)buffer);

    (*env)->CallStaticVoidMethod(env, cEmulator, android_dumpAudio,jbaAudioBuffer,(jint)size);

    if (attached) {
        (*jVM)->DetachCurrentThread(jVM);
    }
}

void myJNI_initInput()
{
    JNIEnv *env;
    (*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4);

#ifdef DEBUG
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "initInput");
#endif

    (*env)->CallStaticVoidMethod(env, cEmulator, android_initInput);
}

/*
The global/local distinction affects both lifetime and scope. A global is usable from any thread, using that thread’s JNIEnv*, and is valid until an explicit call to DeleteGlobalRef(). A local is only usable from the thread it was originally handed to, and is valid until either an explicit call to DeleteLocalRef() or, more commonly, until you return from your native method. When a native method returns, all local references are automatically deleted.
*/

int myJNI_safOpenFile(const char *pathName,const char *mode)
{
    JNIEnv *env;
    (*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4);
    int attached  = 0;

#ifdef DEBUG
//    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "safOpenFile");
#endif
    if(pathName!=NULL)
    {
        //__android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "safOpen %s %s\n",pathName,mode);
        if(env==NULL)
        {
            attached  = 1;
            (*jVM)->AttachCurrentThread(jVM,(void *) &env, NULL);
        }

        jstring jstrBuf1 = (*env)->NewStringUTF(env, pathName);
        jstring jstrBuf2 = (*env)->NewStringUTF(env, mode);
        jint ret =(*env)->CallStaticIntMethod(env, cEmulator, android_safOpenFile, jstrBuf1,jstrBuf2);

        if(attached)
            (*jVM)->DetachCurrentThread(jVM);

        return ret;
    }
    return -1;
    //(*env)->DeleteLocalRef(env, jstrBuf);
}

int myJNI_safReadDir(const char *dirName, int reload)
{
    JNIEnv *env;
    (*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4);
    int attached  = 0;

#ifdef DEBUG
    //__android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "safReadDir");
#endif
    if(dirName!=NULL)
    {
        //__android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "safReadDir %s reload %d\n",dirName, reload);
        if(env==NULL)
        {
            attached  = 1;
            (*jVM)->AttachCurrentThread(jVM,(void *) &env, NULL);
        }

        jstring jstrBuf = (*env)->NewStringUTF(env, dirName);
        jint ret =(*env)->CallStaticIntMethod(env, cEmulator, android_safReadDir, jstrBuf, reload);

        if(attached)
            (*jVM)->DetachCurrentThread(jVM);

        return ret;
    }
    return 0;
    //(*env)->DeleteLocalRef(env, jstrBuf);
}

char **myJNI_safGetNextDirEntry(int id)
{
    JNIEnv *env;
    (*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4);
    int attached  = 0;

#ifdef DEBUG
    //__android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "safGetNextDirEntry");
#endif
    if(id!=0)
    {
        //__android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "safGetNextDirEntry %d\n",id);
        if(env==NULL)
        {
            attached  = 1;
            (*jVM)->AttachCurrentThread(jVM,(void *) &env, NULL);
        }

        jarray data =(*env)->CallStaticObjectMethod(env, cEmulator, android_safGetNextDirEntry, id);

        char** args = NULL;

        if(data!=NULL)
        {
            jsize const length = (*env)->GetArrayLength(env, data);

            args = malloc(sizeof(char*)*length);

            for(int index = 0; index < length; index++ )
            {
                jstring element = (*env)->GetObjectArrayElement(env, data, index);
                char *tmp = (char *) (*env)->GetStringUTFChars(env, element, 0);
                args[index] = (char *) malloc(strlen(tmp) + 1);
                strcpy(args[index], tmp);
                (*env)->ReleaseStringUTFChars(env, element, tmp);
                (*env)->DeleteLocalRef(env, element );
            }
        }

        if(attached)
            (*jVM)->DetachCurrentThread(jVM);

        return args;
    }
    return NULL;
    //(*env)->DeleteLocalRef(env, jstrBuf);
}

void myJNI_safCloseDir(int id)
{
    JNIEnv *env;
    (*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4);
    int attached  = 0;

#ifdef DEBUG
    //__android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "safCloseDir");
#endif
    if(id!=0)
    {
        //__android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "safCloseDir %d\n",id);
        if(env==NULL)
        {
            attached  = 1;
            (*jVM)->AttachCurrentThread(jVM,(void *) &env, NULL);
        }

        (*env)->CallStaticVoidMethod(env, cEmulator, android_safCloseDir, id);

        if(attached)
            (*jVM)->DetachCurrentThread(jVM);
    }

    //(*env)->DeleteLocalRef(env, jstrBuf);
}

int JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv *env;
    jVM = vm;

#ifdef DEBUG
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "JNI_OnLoad called");
#endif

    if((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Failed to get the environment using GetEnv()");
        return -1;
    }

    cEmulator = (*env)->FindClass (env, "com/seleuco/mame4droid/Emulator");

    if(cEmulator==NULL)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Failed to find class com.seleuco.mame4droid.Emulator");
        return -1;
    }

    cEmulator = (jclass) (*env)->NewGlobalRef(env,cEmulator );

    android_dumpVideo = (*env)->GetStaticMethodID(env,cEmulator,"requestRenderFrame","()V");

    if(android_dumpVideo==NULL)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Failed to find method requestRenderFrame");
        return -1;
    }

    android_changeVideo = (*env)->GetStaticMethodID(env,cEmulator,"changeVideo","(IIII)V");

    if(android_changeVideo==NULL)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Failed to find method changeVideo");
        return -1;
    }

    //android_dumpAudio = (*env)->GetStaticMethodID(env,cEmulator,"writeAudio","(Ljava/nio/ByteBuffer;I)V");
    android_dumpAudio = (*env)->GetStaticMethodID(env,cEmulator,"writeAudio","([BI)V");

    if(android_dumpAudio==NULL)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Failed to find method writeAudio");
        return -1;
    }

    android_openAudio = (*env)->GetStaticMethodID(env,cEmulator,"initAudio","(IZ)V");

    if(android_openAudio==NULL)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Failed to find method openAudio");
        return -1;
    }

    android_closeAudio = (*env)->GetStaticMethodID(env,cEmulator,"endAudio","()V");

    if(android_closeAudio==NULL)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Failed to find method closeAudio");
        return -1;
    }

    android_initInput= (*env)->GetStaticMethodID(env,cEmulator,"initInput","()V");

    if(android_initInput==NULL)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Failed to find method initInput");
        return -1;
    }

    android_safOpenFile = (*env)->GetStaticMethodID(env,cEmulator,"safOpenFile","(Ljava/lang/String;Ljava/lang/String;)I");

    if(android_safOpenFile==NULL)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Failed to find method safOpenFile");
        return -1;
    }

    android_safReadDir = (*env)->GetStaticMethodID(env,cEmulator,"safReadDir","(Ljava/lang/String;I)I");

    if(android_safReadDir==NULL)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Failed to find method safReadDir");
        return -1;
    }

    android_safGetNextDirEntry = (*env)->GetStaticMethodID(env,cEmulator,"safGetNextDirEntry","(I)[Ljava/lang/String;");

    if(android_safGetNextDirEntry==NULL)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Failed to find method safGetNextDirEntry");
        return -1;
    }

    android_safCloseDir = (*env)->GetStaticMethodID(env,cEmulator,"safCloseDir","(I)V");

    if(android_safCloseDir==NULL)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Failed to find method safCloseDir");
        return -1;
    }

    return JNI_VERSION_1_4;
}


void* app_Thread_Start(void* args)
{
    android_main(0, NULL);
    return NULL;
}

JNIEXPORT void JNICALL Java_com_seleuco_mame4droid_Emulator_init
  (JNIEnv *env, jclass c,  jstring s1, jstring s2,jint nativeWidth, jint nativeHeight)
{
    __android_log_print(ANDROID_LOG_INFO, "mame4droid-jni", "init");

    const char *str1 = (*env)->GetStringUTFChars(env, s1, 0);

    load_lib(str1);

    (*env)->ReleaseStringUTFChars(env, s1, str1);

    __android_log_print(ANDROID_LOG_INFO, "mame4droid-jni","calling setVideoCallbacks");
    if(setVideoCallbacks!=NULL)
        setVideoCallbacks(&myJNI_dumpVideo,&myJNI_changeVideo);

    __android_log_print(ANDROID_LOG_INFO, "mame4droid-jni","calling setAudioCallbacks");
    if(setAudioCallbacks!=NULL)
       setAudioCallbacks(&myJNI_openAudio,&myJNI_dumpAudio,&myJNI_closeAudio);

    __android_log_print(ANDROID_LOG_INFO, "mame4droid-jni","calling setInputCallbacks");
    if(setInputCallbacks!=NULL)
        setInputCallbacks(&myJNI_initInput);

    __android_log_print(ANDROID_LOG_INFO, "mame4droid-jni","calling setSAFCallbacks");
    if(setSAFCallbacks!=NULL)
        setSAFCallbacks(&myJNI_safOpenFile,&myJNI_safReadDir,&myJNI_safGetNextDirEntry,&myJNI_safCloseDir);

    const char *str2 = (*env)->GetStringUTFChars(env, s2, 0);

    __android_log_print(ANDROID_LOG_INFO, "mame4droid-jni", "path %s",str2);

    if(initMyOSD!=NULL) {
        initMyOSD(str2,nativeWidth, nativeHeight);
    } else{
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni","Not initMyOSD!!!");
    }

    (*env)->ReleaseStringUTFChars(env, s2, str2);

    //int i = pthread_create(&main_tid, NULL, app_Thread_Start, NULL);

    //if(i!=0)__android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Error setting creating pthread %d",i);
    //struct sched_param    param;
    //param.sched_priority = 63;
    //param.sched_priority = 46;
    //param.sched_priority = 100;
    /*
    if(pthread_setschedparam(main_tid, SCHED_RR, &param) != 0)
    {
        __android_log_print(ANDROID_LOG_ERROR, "mame4droid-jni", "Error setting pthread priority");
        return;
    }
    */
}

JNIEXPORT void JNICALL Java_com_seleuco_mame4droid_Emulator_setDigitalData
  (JNIEnv *env, jclass c, jint i,  jlong jl)
{
    //long 	jlong 	signed 64 bits ??? valdria con un jint
    //__android_log_print(ANDROID_LOG_INFO, "mame4droid-jni", "setPadData");

    unsigned long l = (unsigned long)jl;

    if(setDigitalData!=NULL)
       setDigitalData(i,l);
    else
      __android_log_print(ANDROID_LOG_WARN, "mame4droid-jni", "error no setDigitalData!");
}

JNIEXPORT void JNICALL Java_com_seleuco_mame4droid_Emulator_setAnalogData
  (JNIEnv *env, jclass c, jint i, jfloat v1, jfloat v2)
{
    if(setAnalogData!=NULL)
       setAnalogData(i,v1,v2);
    else
      __android_log_print(ANDROID_LOG_WARN, "mame4droid-jni", "error no setAnalogData!");
}

JNIEXPORT jint JNICALL Java_com_seleuco_mame4droid_Emulator_getValue
  (JNIEnv *env, jclass c, jint key, jint i)
{
#ifdef DEBUG
   // __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "getValue %d",key);
#endif
      if(getMyValue!=NULL)
         return getMyValue(key,i);
      else
      {
         __android_log_print(ANDROID_LOG_WARN, "mame4droid-jni", "error no getMyValue! key:%d:%d",key,i);
         return -1;
      }
}

JNIEXPORT void JNICALL Java_com_seleuco_mame4droid_Emulator_setValue
  (JNIEnv *env, jclass c, jint key, jint i, jint value)
{
#ifdef DEBUG
    //__android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "setValue %d,%d=%d",key,i,value);
#endif
    if(setMyValue!=NULL)
      setMyValue(key,i,value);
    else
      __android_log_print(ANDROID_LOG_WARN, "mame4droid-jni", "error no setMyValue!");
}

JNIEXPORT jstring JNICALL Java_com_seleuco_mame4droid_Emulator_getValueStr
  (JNIEnv *env, jclass c, jint key, jint i)
{
#ifdef DEBUG
   // __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "getValueStr %d",key);
#endif
      if(getMyValueStr!=NULL)
      {
         const char * r =  getMyValueStr(key,i);
         return (*env)->NewStringUTF(env,r);
      }
      else
      {
         __android_log_print(ANDROID_LOG_WARN, "mame4droid-jni", "error no getMyValueStr!");
         return NULL;
      }
}

JNIEXPORT void JNICALL Java_com_seleuco_mame4droid_Emulator_setValueStr
  (JNIEnv *env, jclass c, jint key, jint i, jstring s1)
{
    if(setMyValueStr!=NULL)
    {
       const char *value = (*env)->GetStringUTFChars(env, s1, 0);
#ifdef DEBUG
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "setValueStr %d,%d=%s",key,i,value);
#endif
       setMyValueStr(key,i,value);
       (*env)->ReleaseStringUTFChars(env, s1, value);
    }
    else
      __android_log_print(ANDROID_LOG_WARN, "mame4droid-jni", "error no setMyValueStr!");
}

JNIEXPORT void JNICALL Java_com_seleuco_mame4droid_Emulator_runT
  (JNIEnv *env, jclass c){
#ifdef DEBUG
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "runThread");
#endif
    if(android_main!=NULL)
       android_main(0, NULL);
    else
       __android_log_print(ANDROID_LOG_WARN, "mame4droid-jni", "error no android main!");
}

JNIEXPORT int JNICALL Java_com_seleuco_mame4droid_Emulator_setKeyData
        (JNIEnv *env, jclass c, jint keyCode, jint keyAction, jchar keyChar){
#ifdef DEBUG
     //__android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "setKeyData %d %d %c",keyCode, keyAction, keyChar);
#endif
    if(setKeyData!=NULL)
        return setKeyData(keyCode, keyAction , keyChar);
    else
        __android_log_print(ANDROID_LOG_WARN, "mame4droid-jni", "error no setKeyData!");
    return 0;
}

JNIEXPORT jint JNICALL Java_com_seleuco_mame4droid_Emulator_setMouseData
        (JNIEnv *env, jclass c, jint i, jint mouseAction, jint button, jfloat cx, jfloat cy){
#ifdef DEBUG
    //__android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "setMouseData %d %d %d",mouseAction, cx, cy);
#endif
    if(setMouseData!=NULL)
        return setMouseData(i, mouseAction, button , cx, cy);
    else
        __android_log_print(ANDROID_LOG_WARN, "mame4droid-jni", "error no setMouseData!");
    return 0;
}

JNIEXPORT jint JNICALL Java_com_seleuco_mame4droid_Emulator_setTouchData
        (JNIEnv *env, jclass c, jint i, jint touchAction, jfloat cx, jfloat cy){
#ifdef DEBUG
    __android_log_print(ANDROID_LOG_DEBUG, "mame4droid-jni", "setTouchData %d %d %d",touchAction, (int)cx, (int)cy);
#endif
    if(setTouchData!=NULL)
        return setTouchData(i, touchAction, cx, cy);
    else
        __android_log_print(ANDROID_LOG_WARN, "mame4droid-jni", "error no setTouchData!");
    return 0;
}

JNIEXPORT void JNICALL Java_com_seleuco_mame4droid_Emulator_onSurfaceCreated
	(JNIEnv* env, jclass c)
{
    if (onSurfaceCreated != NULL)
	    onSurfaceCreated();
}

JNIEXPORT void JNICALL Java_com_seleuco_mame4droid_Emulator_onDrawFrame
	(JNIEnv* env, jclass c)
{
    if (onDrawFrame != NULL)
	    onDrawFrame();
}

JNIEXPORT void JNICALL Java_com_seleuco_mame4droid_Emulator_onChooseRenderer
        (JNIEnv* env, jclass c, jint renderer)
{
    if (onChooseRenderer != NULL)
        onChooseRenderer(renderer);
}

JNIEXPORT jobjectArray JNICALL Java_com_seleuco_mame4droid_Emulator_getShaders
        (JNIEnv* env, jclass c, jint renderer)
{
    const char** shader_list;
    int n;

    getShaders(renderer, &shader_list, &n);

    jobjectArray array = (*env)->NewObjectArray(env, n, (*env)->FindClass(env, "java/lang/String"), NULL);
    for (int i=0; i<n; i++)
    {
        jstring str = (*env)->NewStringUTF(env, shader_list[i]);
        (*env)->SetObjectArrayElement(env, array, i, str);
    }

    return array;
}

JNIEXPORT jboolean JNICALL Java_com_seleuco_mame4droid_Emulator_setShader
        (JNIEnv* env, jclass c, jstring shader_name)
{
    bool ret;
    if (shader_name == NULL)
    {
        ret = setShader(NULL);
    }
    else
    {
        const char *temp = (*env)->GetStringUTFChars(env, shader_name, NULL);
        ret = setShader(temp);
        (*env)->ReleaseStringUTFChars(env, shader_name, temp);
    }

    return ret;
}

JNIEXPORT void JNICALL Java_com_seleuco_mame4droid_Emulator_loadShaders
        (JNIEnv* env, jclass c, jstring path)
{
    static bool loaded = false;

    if (!loaded)
    {
        const char *temp = (*env)->GetStringUTFChars(env, path, NULL);
        loadShaders(temp);
        (*env)->ReleaseStringUTFChars(env, path, temp);

        loaded = true;
    }
}