/**
 * 实现opensles+FFmpeg播放的实现方法一
 * 通过av_read_frame读取音频像素数据，遍历将或者的数据转pcm，再由opensles进行勃发
 */



#include <jni.h>
//--------------------------安卓的log
#include <android/log.h>
#include <unistd.h>

#define LOG_TAG "jason"
#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
//--------------------------安卓的log
extern "C"
{
//-------------------------ndk的opensles
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
//-------------------------ndk的opensles

//memcpy
#include <string.h>
//<sys/type.h>是unix/linux系统基本数据类型的头文件
#include <sys/types.h>

//--------------------------ffmpeg
//格式封装
#include <libavformat/avformat.h>
//编解码
#include <libavcodec/avcodec.h>
//重采样
#include <libswresample/swresample.h>
//设置采样参数
#include <libavutil/opt.h>
//--------------------------ffmpeg

}





extern "C" {

/**
 * 创建FFmpeg解码器
 * @return
 */
int createFFmpeg(JNIEnv *env, jstring srcPath);

/**
创建opensles的引擎，结果返回0失败1成功
*/
int createOpenslEngine();

/**
创建opensles的缓存队列播放器，结果返回0失败1成功
*/
int createBufferQueue(int sampleRate, int channels);

/**
BufferQueue的回调函数：每次在buffer播放完成后都会调用该函数
*/
void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context);

/**
释放资源
*/
void releaseResource();

//FFmpeg
static const enum AVSampleFormat dst_sample_fmt = AV_SAMPLE_FMT_S16;
static AVCodecContext *avCodecContext;
static AVFormatContext *avFormatContext = NULL;//必须初始化不然open_input会挂
static uint8_t *out_buffer = NULL;
static AVPacket *avPacket;
static AVFrame *avFrame;
static struct SwrContext *swrContext;
static int outChannelCount;
static int streamIndex = -1;
int currentIndex = 0;


//engine interface
static SLObjectItf engineObject = NULL;
static SLEngineItf engineEngine;

//outout mix
static SLObjectItf outputMixObject = NULL;
static SLEnvironmentalReverbItf outputMixEnvironmentalReverb = NULL;
//stone corridor 
static SLEnvironmentalReverbSettings reverbSettings = SL_I3DL2_ENVIRONMENT_PRESET_STONECORRIDOR;

// buffer queue player interfaces
static SLObjectItf bqPlayerObject = NULL;
static SLPlayItf bqPlayerPlay;
static SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;

JNIEXPORT void
Java_com_jason_ndk_ffmpeg_openeles_MainActivity_sound(JNIEnv *env, jobject thiz, jstring input) {


    int ret = createFFmpeg(env, input);

    if(ret < 0) {
        LOGE("创建FFmpeg失败");
        releaseResource();
        return;
    }



    //初始化opensles
    ret = createOpenslEngine();
    if (ret == JNI_FALSE) {
        LOGE("创建opensles引擎失败");
        releaseResource();
        return;
    }

    ret = createBufferQueue(avCodecContext->sample_rate, outChannelCount);
    if (ret == JNI_FALSE) {
        LOGE("创建buffer queue播放器失败");
        releaseResource();
        return;
    }

    LOGD("start av_read_frame");
    //主动调用回调函数
    bqPlayerCallback(bqPlayerBufferQueue, (void *) "0");


}


int createFFmpeg(JNIEnv *env, jstring srcPath) {

    const char * originPath = env->GetStringUTFChars(srcPath, NULL);
    //创建avFormatContext对象
    avFormatContext = avformat_alloc_context();
    int ret = avformat_open_input(&avFormatContext, originPath, NULL, NULL);
    if(ret != 0) {
        LOGE("打开文件失败");
        return -1;
    }

    //输出文件信息
    av_dump_format(avFormatContext, 0, originPath, 0);

    ret = avformat_find_stream_info(avFormatContext, NULL);
    if(ret < 0) {
        LOGE("获取编码流信息失败");
        return -1;
    }

    //获取当前的类型流的索引位置
    for (int i = 0; i < avFormatContext->nb_streams; ++i) {
        //获取流的编码类型
        enum AVMediaType avMediaType = avFormatContext->streams[i]->codecpar->codec_type;
        if(avMediaType == AVMEDIA_TYPE_AUDIO) {
            streamIndex = i;
            break;
        }
    }

    //根据类型流对应的索引位置，获取对应的类型解码器
    AVCodecParameters *avCodecParameters = avFormatContext->streams[streamIndex]->codecpar;
    //获取对应类型的解码器id
    AVCodecID avCodecId = avCodecParameters->codec_id;

    //获取解码器
    AVCodec *avCodec = avcodec_find_decoder(avCodecId);

    //创建解码器上下文
    avCodecContext = avcodec_alloc_context3(NULL);
    if(avCodecContext == NULL) {
        LOGE("创建解码器上下文失败");
        return -1;
    }

    //将AVCodecParameter的相关内容-->AVCodecContext
    avcodec_parameters_to_context(avCodecContext, avCodecParameters);
    // 打开解码器
    ret = avcodec_open2(avCodecContext, avCodec,  NULL);
    if(ret < 0) {
        LOGE("打开解码器失败");
        return -1;
    }

    //创建源文件解码的压缩数据包对象
    avPacket = static_cast<AVPacket *>(av_mallocz(sizeof(AVPacket)));
    //创建一个用于存放解码之后的像素数据
    avFrame = av_frame_alloc();
    //创建SwrContext对象，分配空间
    swrContext = swr_alloc();
    // 原音频的采样编码格式
    AVSampleFormat srcFormat = avCodecContext->sample_fmt;
    // 生成目标采样编码格式
    AVSampleFormat  dstFormat = dst_sample_fmt;
    // 原音频的采样率
    int srcSampleRate = avCodecContext->sample_rate;
    // 生成目标采样率
    int disSampleRate = 48000;
    // 输入声道布局
    uint64_t src_ch_layout = avCodecContext->channel_layout;
    // 输出声道布局
    uint64_t dst_ch_layout = AV_CH_LAYOUT_STEREO;
    // 给Swrcontext 分配空间，设置公共参数
    swr_alloc_set_opts(swrContext, dst_ch_layout, dstFormat, disSampleRate,
                       src_ch_layout, srcFormat, srcSampleRate, 0, NULL
    );
    //SwrContext进行初始化
    swr_init(swrContext);

    // 获取声道数量
    outChannelCount = av_get_channel_layout_nb_channels(dst_ch_layout);
    LOGD("声道数量%d ", outChannelCount);
    // 设置音频缓冲区间 16bit   48000  PCM数据, 双声道
    out_buffer = (uint8_t *) av_malloc(2 * 48000);
    env->ReleaseStringUTFChars(srcPath, originPath);
    return 0;
}

void releaseResource() {
    // destroy buffer queue audio player object, and invalidate all associated interfaces
    if (bqPlayerObject != NULL) {
        (*bqPlayerObject)->Destroy(bqPlayerObject);
        bqPlayerObject = NULL;
        bqPlayerPlay = NULL;
        bqPlayerBufferQueue = NULL;
    }

    if (NULL != outputMixObject) {
        (*outputMixObject)->Destroy(outputMixObject);
        outputMixObject = NULL;
        outputMixEnvironmentalReverb = NULL;
    }

    if (engineObject != NULL) {
        (*engineObject)->Destroy(engineObject);
        engineObject = NULL;
        engineEngine = NULL;
    }

    avcodec_free_context(&avCodecContext);
    avformat_close_input(&avFormatContext);
    av_free(out_buffer);
    av_frame_free(&avFrame);
    swr_free(&swrContext);
}


/*JNI_FALSE=0 JNI_SUCCESS=1*/
int createOpenslEngine() {
    SLresult result;
    //线程安全
    const SLEngineOption engineOptions[1] = {{(SLuint32) SL_ENGINEOPTION_THREADSAFE, (SLuint32) SL_BOOLEAN_TRUE}};
    //该函数表示：初始化引擎对象给使用者一个处理手柄对象，第四个参数（需要支持的interface数目）为零则会忽视第五、第六个参数
    result = slCreateEngine(&engineObject, 1, engineOptions, 0, NULL, NULL);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("opensl es引擎创建初始化失败");
        return JNI_FALSE;
    }

    //该函数表示：转化一个Object从未实例化到实例化过程，第二个参数表示是否异步
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("引擎Object实例化失败");
        return JNI_FALSE;
    }

    //该函数表示：得到由Object暴露的接口，这里指的是引擎接口，第二个参数是接口ID,第三个参数是输出的引擎接口对象
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("引擎接口获取失败");
        return JNI_FALSE;
    }

    //该函数表示：创建输出混音器--->由引擎接口创建，从第三个参数开始就是支持的interface数目，同样的为零忽略第四第五个参数
    const SLInterfaceID interfaceIds[1] = {SL_IID_ENVIRONMENTALREVERB};//这里给一个环境混响的接口id
    const SLboolean reqs[1] = {SL_BOOLEAN_TRUE};
    result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 1, interfaceIds,
                                              reqs);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("创建输出混音器失败");
        return JNI_FALSE;
    }

    //同样的实例化输出混音器对象
    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("输出混音器outout mix实例化失败");
        return JNI_FALSE;
    }

    //因为环境混响接口的失败与否没关系的
    //同样的申请支持了环境混响EnvironmentalReverb接口就可以获取该接口对象
    result = (*outputMixObject)->GetInterface(outputMixObject, SL_IID_ENVIRONMENTALREVERB,
                                              &outputMixEnvironmentalReverb);
    if (result == SL_RESULT_SUCCESS) {
        result = (*outputMixEnvironmentalReverb)->SetEnvironmentalReverbProperties(
                outputMixEnvironmentalReverb, &reverbSettings);
        if (result != SL_RESULT_SUCCESS) {
            LOGD("混响属性设置失败");
        }
    } else {
        LOGD("获取环境混响接口失败");
    }

    return JNI_TRUE;
}

/**
创建pcm播放格式：采样率、通道数、单个样本的比特率（s16le）
*/
int createBufferQueue(int sampleRate, int channels) {
    SLresult result;

    // configure audio source
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};


    int numChannels = 2;
    SLuint32 samplesPerSec = SL_SAMPLINGRATE_48;//注意是毫秒赫兹
    SLuint32 bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
    SLuint32 containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
    //引文channels=2，native-audio-jni.c中的例子是单声道的所以取SL_SPEAKER_FRONT_CENTER
    SLuint32 channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    SLuint32 endianness = SL_BYTEORDER_LITTLEENDIAN;

    numChannels = channels;

    if (channels == 1) {
        channelMask = SL_SPEAKER_FRONT_CENTER;
    } else {
        //2以及更多
        channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    }

    SLDataFormat_PCM format_pcm = {SL_DATAFORMAT_PCM, (SLuint32) numChannels, samplesPerSec,
                                   bitsPerSample, containerSize, channelMask, endianness};

    SLDataSource audioSrc = {&loc_bufq, &format_pcm};

    // configure audio sink
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataSink audioSnk = {&loc_outmix, NULL};

    // create audio player
    const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    result = (*engineEngine)->CreateAudioPlayer(engineEngine, &bqPlayerObject, &audioSrc, &audioSnk,
                                                1, ids, req);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("创建audioplayer失败");
        return JNI_FALSE;
    }


    result = (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("实例化audioplayer失败");
        return JNI_FALSE;
    }

    LOGD("---createBufferQueueAudioPlayer---");

    // get the play interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerPlay);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("获取play接口对象失败");
        return JNI_FALSE;
    }

    // get the buffer queue interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE,
                                             &bqPlayerBufferQueue);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("获取BUFFERQUEUE接口对象失败");
        return JNI_FALSE;
    }

    // register callback on the buffer queue   这边的缓冲回调跟，主动的调用不是一个线程
    /*result = (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, bqPlayerCallback,
                                                      (void *) "1");
    if (result != SL_RESULT_SUCCESS) {
        LOGD("获取play接口对象失败");
        return JNI_FALSE;
    }*/

    // set the player's state to playing
    result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("设置为可播放状态失败");
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    char * args = (char *)context;
    if (strcmp(args, "1") == 0){
        LOGE("来自缓冲的回调");
        return;
    }
    LOGE("主动触发");

    while (av_read_frame(avFormatContext, avPacket) >= 0) {
        if (avPacket->stream_index == streamIndex) {
            int ret = avcodec_send_packet(avCodecContext, avPacket);
            currentIndex ++;
            while (ret >= 0) {
                ret = avcodec_receive_frame(avCodecContext, avFrame);
                swr_convert(swrContext, &out_buffer, 2 * 48000,
                            (const uint8_t **) avFrame->data, avFrame->nb_samples);

                //该函数表示：通过给定的参数得到需要的buffer size
                int dst_buffer_size = av_samples_get_buffer_size(NULL, outChannelCount, avFrame->nb_samples, AV_SAMPLE_FMT_S16, 1);
                if (dst_buffer_size <= 0) {
                    break;
                }

                //MP3每帧是1152字节，ACC每帧是1024/2048字节
                LOGD("WRITE TO AUDIOTRACK %d", dst_buffer_size);//4608

                SLresult result;
                result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, out_buffer, dst_buffer_size);
                if (result != SL_RESULT_SUCCESS) {
                    LOGD("入队失败");
                }
                //计算公式 acc  1024 * 1000 / 48000 = 21.34
                //计算公式 mp3  1152 * 1000 / 48000 = 24
                usleep(1000 * 24);
            }
            LOGE("正在解码%d", currentIndex++);
        }
    }

}

}
extern "C"
JNIEXPORT void JNICALL
Java_com_jason_ndk_ffmpeg_openeles_MainActivity_stop(JNIEnv *env, jobject thiz) {
    releaseResource();
}