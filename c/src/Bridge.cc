/*
** Copyright (C) 2013 Mellanox Technologies
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at:
**
** http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
** either express or implied. See the License for the specific language
** governing permissions and  limitations under the License.
**
*/

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <jni.h>

#include <libxio.h>

#include "CallbackFunctions.h"
#include "Client.h"
#include "Server.h"

#include "Context.h"
#include "Utils.h"
#include "MsgPool.h"


static jclass cls;
static JavaVM *cached_jvm;


static jclass cls_data;

static jfieldID fidPtr;
static jfieldID fidBuf;
static jfieldID fidError;


// JNI inner functions implementations
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void* reserved)
{
	printf("in in JXIO/c/Bridge - JNI_OnLoad\n");

	cached_jvm = jvm;
	JNIEnv *env;
	if (jvm->GetEnv((void **)&env, JNI_VERSION_1_4)) { //direct buffer requires java 1.4
		return JNI_ERR; /* JNI version not supported */
	}

	cls = env->FindClass("com/mellanox/jxio/impl/Bridge");
	if (cls == NULL) {
		fprintf(stderr, "in JXIO/c/Bridge - java class was NOT found\n");
		return JNI_ERR;
	}

	cls_data = env->FindClass("com/mellanox/jxio/EventQueueHandler$DataFromC");
	if (cls_data == NULL) {
		fprintf(stderr, "in JXIO/c/Bridge - java class was NOT found\n");
		return JNI_ERR;
	}

	if (fidPtr == NULL) {
	fidPtr = env->GetFieldID(cls_data, "ptrCtx", "J");
	if (fidPtr == NULL) {
		fprintf(stderr, "in JXIO/c/Bridge - could not get field ptrCtx\n");
		}
	}

	if (fidBuf == NULL) {
	fidBuf = env->GetFieldID(cls_data, "eventQueue","Ljava/nio/ByteBuffer;");
	if (fidBuf == NULL) {
		fprintf(stderr, "in JXIO/c/Bridge - could not get field fidBuf\n");
		}
	}

	printf("in JXIO/c/Bridge - java callback methods were found and cached\n");

	return JNI_VERSION_1_4;  //direct buffer requires java 1.4
}

extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *jvm, void* reserved)
{
	// NOTE: We never reached this place
	return;
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_mellanox_jxio_impl_Bridge_createCtxNative(JNIEnv *env, jclass cls, jint eventQueueSize, jobject dataToC)
{
	int size = eventQueueSize;
	Context *ctx = new Context (size);
	if (ctx == NULL) {
		log(lsERROR, "memory allocation failed\n");
		return false;
	}
	if (ctx->error_creating) {
		delete (ctx);
		return false;
	}
	jobject jbuf = env->NewDirectByteBuffer(ctx->event_queue->get_buffer(), eventQueueSize );

	jlong ptr = (jlong)(intptr_t) ctx;

	env->SetLongField(dataToC, fidPtr, ptr);
	env->SetObjectField(dataToC, fidBuf, jbuf);

	return ctx->error_creating;
}

extern "C" JNIEXPORT void JNICALL Java_com_mellanox_jxio_impl_Bridge_closeCtxNative(JNIEnv *env, jclass cls, jlong ptrCtx)
{
	Context *ctx = (Context *)ptrCtx;
	delete (ctx);
	log(lsDEBUG, "end of closeCTX\n");
}

extern "C" JNIEXPORT jint JNICALL Java_com_mellanox_jxio_impl_Bridge_runEventLoopNative(JNIEnv *env, jclass cls, jlong ptrCtx, jlong timeOutMicroSec)
{
	Context *ctx = (Context *)ptrCtx;
	return ctx->run_event_loop((long)timeOutMicroSec);
}

extern "C" JNIEXPORT void JNICALL Java_com_mellanox_jxio_impl_Bridge_breakEventLoopNative(JNIEnv *env, jclass cls, jlong ptrCtx)
{
	Context *ctx = (Context *)ptrCtx;
	ctx->break_event_loop();
}

extern "C" JNIEXPORT jint JNICALL Java_com_mellanox_jxio_impl_Bridge_addEventLoopFdNative(JNIEnv *env, jclass cls, jlong ptrCtx, jint fd, jint events, jlong priv_data)
{
	Context *ctx = (Context *)ptrCtx;
	return ctx->add_event_loop_fd(fd, events, (void*)priv_data);
}

extern "C" JNIEXPORT jint JNICALL Java_com_mellanox_jxio_impl_Bridge_delEventLoopFdNative(JNIEnv *env, jclass cls, jlong ptrCtx, jint fd)
{
	Context *ctx = (Context *)ptrCtx;
	return ctx->del_event_loop_fd(fd);
}

extern "C" JNIEXPORT jlong JNICALL Java_com_mellanox_jxio_impl_Bridge_startSessionClientNative(JNIEnv *env, jclass cls, jstring jurl, jlong ptrCtx)
{
	const char *url = env->GetStringUTFChars(jurl, NULL);
	Client * ses = new Client(url, ptrCtx);
	env->ReleaseStringUTFChars(jurl, url);

	if (ses == NULL) {
		log(lsERROR, "memory allocation failed\n");
		return NULL;
	}
	if (ses->error_creating) {
		delete (ses);
		return 0;
	}
	return (jlong)(intptr_t) ses;
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_mellanox_jxio_impl_Bridge_closeSessionClientNative(JNIEnv *env, jclass cls, jlong ptrSes)
{
	Client *ses = (Client*)ptrSes;
	return ses->close_connection();
}

extern "C" JNIEXPORT jlongArray JNICALL Java_com_mellanox_jxio_impl_Bridge_startServerNative(JNIEnv *env, jclass cls, jstring jurl, jlong ptrCtx)
{
	jlong temp[2];

	jlongArray dataToJava = env->NewLongArray(2);
	if (dataToJava == NULL) {
		return NULL; /* out of memory error thrown */
	}

	const char *url = env->GetStringUTFChars(jurl, NULL);

	Server *server = new Server(url, ptrCtx);
	env->ReleaseStringUTFChars(jurl, url);

	if (server == NULL) {
		log(lsERROR, "memory allocation failed\n");
		return NULL;
	}
	if (server->error_creating) {
		temp[0] = 0;
		temp[1]=0;
		delete(server);
	} else {
		temp[0] = (jlong)(intptr_t) server;
		temp[1] = (jlong)server->port;
	}

	env->SetLongArrayRegion(dataToJava,0, 2, temp);
	return dataToJava;
}

extern "C" JNIEXPORT void JNICALL Java_com_mellanox_jxio_impl_Bridge_stopServerNative(JNIEnv *env, jclass cls, jlong ptrServer)
{
	Server *server = (Server *)ptrServer;
	server->close();
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_mellanox_jxio_impl_Bridge_forwardSessionNative(JNIEnv *env, jclass cls, jstring jurl, jlong ptr_session, jlong ptr_server)
{
	const char *url = env->GetStringUTFChars(jurl, NULL);
	struct xio_session *session = (struct xio_session *)ptr_session;
	Server * server = (Server *) ptr_server;
	bool retVal = server->forward(session, url);
	env->ReleaseStringUTFChars(jurl, url);

	return retVal;
}


extern "C" JNIEXPORT jobject JNICALL Java_com_mellanox_jxio_impl_Bridge_createMsgPoolNative(JNIEnv *env, jclass cls, jint msg_num, jint in_size, jint out_size, jlongArray ptr)
{
	jlong temp[msg_num+1];
	MsgPool *pool = new MsgPool (msg_num, in_size, out_size);
	if (pool == NULL) {
		log(lsERROR, "memory allocation failed\n");
		return NULL;
	}
	if (pool->error_creating) {
		log(lsERROR, "there was an error creating MsgPool\n");
		delete (pool);
		return NULL;
	}
	jobject jbuf = env->NewDirectByteBuffer(pool->buf, pool->buf_size);

	//TODO: go over the actual data structure and not msg_ptrs
	temp [0] = (jlong)(intptr_t) pool; //the first element in array represents ptr to MsgPool
	for (int i=1; i<=msg_num; i++){
		temp[i] = (jlong)(intptr_t) pool->msg_ptrs[i-1];
	}

	env->SetLongArrayRegion(ptr,0, msg_num+1, temp);
	return jbuf;
}


extern "C" JNIEXPORT jboolean JNICALL Java_com_mellanox_jxio_impl_Bridge_sendMsgNative(JNIEnv *env, jclass cls, jlong ptr_session, jint session_type, jlong ptr_msg)
{
	Msg * msg = (Msg*) ptr_msg;

	if (session_type == 0){//it's server
		Server * server = (Server *) ptr_session;
		return server->send_reply(msg);
	}else{ //it's client
		Client * client = (Client *) ptr_session;
		return client->send_msg(msg);
	}
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_mellanox_jxio_impl_Bridge_bindMsgPoolNative(JNIEnv *env, jclass cls, jlong ptr_msg_pool, jlong ptr_ctx)
{
	Context * ctx = (Context*) ptr_ctx;
	MsgPool * pool = (MsgPool*) ptr_msg_pool;
	ctx->add_msg_pool(pool);
	return true;
}

extern "C" JNIEXPORT jstring JNICALL Java_com_mellanox_jxio_impl_Bridge_getErrorNative(JNIEnv *env, jclass cls, jint errorReason)
{
	struct xio_session *session;
	const char * error;
	jstring str;

	error = xio_strerror(errorReason);
	str = env->NewStringUTF(error);
//	free(error); TODO: to free it????
	return str;
}

JNIEnv *JX_attachNativeThread()
{
	JNIEnv *env;
	if (! cached_jvm) {
		printf("cached_jvm is NULL");
	}
	jint ret = cached_jvm->AttachCurrentThread((void **)&env, NULL);

	if (ret < 0) {
		printf("cached_jvm->AttachCurrentThread failed ret=%d", ret);
	}
	printf("completed successfully env=%p", env);
	return env; // note: this handler is valid for all functions in this thread
}

