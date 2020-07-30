/**
 * 实现opensles+FFmpeg播放的实现方法一
 * 利用缓存回调进行音频播放
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
int createFFmpeg2(JNIEnv *env, jstring srcPath);

void getPCM(void **outBuffer, size_t *size);

/**
创建opensles的引擎，结果返回0失败1成功
*/
int createOpenslEngine2();

/**
创建opensles的缓存队列播放器，结果返回0失败1成功
*/
int createBufferQueue2(int sampleRate, int channels);

/**
BufferQueue的回调函数：每次在buffer播放完成后都会调用该函数
*/
void bqPlayerCallback2(SLAndroidSimpleBufferQueueItf bq, void *context);

/**
释放资源
*/
void releaseResource2();

//FFmpeg
static const enum AVSampleFormat dst_sample_fmt = AV_SAMPLE_FMT_S16;
static AVCodecContext *avCodecContext2;
static AVFormatContext *avFormatContext2 = NULL;//必须初始化不然open_input会挂
static uint8_t *out_buffer2 = NULL;
static int bufferSize2 = 0;
static AVPacket *avPacket2;
static AVFrame *avFrame2;
static struct SwrContext *swrContext2;
static int outChannelCount2;
static int streamIndex2 = -1;
int currentIndex2 = 0;
size_t bufferLen;
void *buffer;

//engine interface
static SLObjectItf engineObject2 = NULL;
static SLEngineItf engineEngine2;

//outout mix
static SLObjectItf outputMixObject2 = NULL;
static SLEnvironmentalReverbItf outputMixEnvironmentalReverb2 = NULL;
//stone corridor
static SLEnvironmentalReverbSettings reverbSettings2 = SL_I3DL2_ENVIRONMENT_PRESET_STONECORRIDOR;

// buffer queue player interfaces
static SLObjectItf bqPlayerObject2 = NULL;
static SLPlayItf bqPlayerPlay2;
static SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue2;

JNIEXPORT void
Java_com_jason_ndk_ffmpeg_openeles_MainActivity_play2(JNIEnv *env, jobject thiz, jstring input) {

    int ret = createFFmpeg2(env, input);

    if(ret < 0) {
        LOGE("创建FFmpeg失败");
        releaseResource2();
        return;
    }

    //初始化opensles
    ret = createOpenslEngine2();
    if (ret == JNI_FALSE) {
        LOGE("创建opensles引擎失败");
        releaseResource2();
        return;
    }

    ret = createBufferQueue2(avCodecContext2->sample_rate, outChannelCount2);
    if (ret == JNI_FALSE) {
        LOGE("创建buffer queue播放器失败");
        releaseResource2();
        return;
    }

    LOGD("start av_read_frame");
    //主动调用回调函数
    bqPlayerCallback2(bqPlayerBufferQueue2, (void *) "0");
}


int createFFmpeg2(JNIEnv *env, jstring srcPath) {

    const char * originPath = env->GetStringUTFChars(srcPath, NULL);
    //创建avFormatContext对象
    avFormatContext2 = avformat_alloc_context();
    int ret = avformat_open_input(&avFormatContext2, originPath, NULL, NULL);
    if(ret != 0) {
        LOGE("打开文件失败");
        return -1;
    }

    //输出文件信息
    av_dump_format(avFormatContext2, 0, originPath, 0);

    ret = avformat_find_stream_info(avFormatContext2, NULL);
    if(ret < 0) {
        LOGE("获取编码流信息失败");
        return -1;
    }

    //获取当前的类型流的索引位置
    for (int i = 0; i < avFormatContext2->nb_streams; ++i) {
        //获取流的编码类型
        enum AVMediaType avMediaType = avFormatContext2->streams[i]->codecpar->codec_type;
        if(avMediaType == AVMEDIA_TYPE_AUDIO) {
            streamIndex2 = i;
            break;
        }
    }

    //根据类型流对应的索引位置，获取对应的类型解码器
    AVCodecParameters *avCodecParameters = avFormatContext2->streams[streamIndex2]->codecpar;
    //获取对应类型的解码器id
    AVCodecID avCodecId = avCodecParameters->codec_id;

    //获取解码器
    AVCodec *avCodec = avcodec_find_decoder(avCodecId);

    //创建解码器上下文
    avCodecContext2 = avcodec_alloc_context3(NULL);
    if(avCodecContext2 == NULL) {
        LOGE("创建解码器上下文失败");
        return -1;
    }

    //将AVCodecParameter的相关内容-->AVCodecContext
    avcodec_parameters_to_context(avCodecContext2, avCodecParameters);
    // 打开解码器
    ret = avcodec_open2(avCodecContext2, avCodec,  NULL);
    if(ret < 0) {
        LOGE("打开解码器失败");
        return -1;
    }

    //创建源文件解码的压缩数据包对象
    avPacket2 = static_cast<AVPacket *>(av_mallocz(sizeof(AVPacket)));
    //创建一个用于存放解码之后的像素数据
    avFrame2 = av_frame_alloc();
    //创建SwrContext对象，分配空间
    swrContext2 = swr_alloc();
    // 原音频的采样编码格式
    AVSampleFormat srcFormat = avCodecContext2->sample_fmt;
    // 生成目标采样编码格式
    AVSampleFormat  dstFormat = dst_sample_fmt;
    // 原音频的采样率
    int srcSampleRate = avCodecContext2->sample_rate;
    // 生成目标采样率
    int disSampleRate = 48000;
    // 输入声道布局
    uint64_t src_ch_layout = avCodecContext2->channel_layout;
    // 输出声道布局
    uint64_t dst_ch_layout = AV_CH_LAYOUT_STEREO;
    // 给Swrcontext 分配空间，设置公共参数
    swr_alloc_set_opts(swrContext2, dst_ch_layout, dstFormat, disSampleRate,
                       src_ch_layout, srcFormat, srcSampleRate, 0, NULL
    );
    //SwrContext进行初始化
    swr_init(swrContext2);

    // 获取声道数量
    outChannelCount2 = av_get_channel_layout_nb_channels(dst_ch_layout);
    LOGD("声道数量%d ", outChannelCount2);
    // 设置音频缓冲区间 16bit   48000  PCM数据, 双声道
    out_buffer2 = (uint8_t *) av_malloc(2 * 48000);
    env->ReleaseStringUTFChars(srcPath, originPath);
    return 0;
}

void releaseResource2() {
    avcodec_free_context(&avCodecContext2);
    avformat_close_input(&avFormatContext2);
    av_free(out_buffer2);
    av_frame_free(&avFrame2);
    swr_free(&swrContext2);

    // destroy buffer queue audio player object, and invalidate all associated interfaces
    if (bqPlayerObject2 != NULL) {
        (*bqPlayerObject2)->Destroy(bqPlayerObject2);
        bqPlayerObject2 = NULL;
        bqPlayerPlay2 = NULL;
        bqPlayerBufferQueue2 = NULL;
    }

    if (NULL != outputMixObject2) {
        (*outputMixObject2)->Destroy(outputMixObject2);
        outputMixObject2 = NULL;
        outputMixEnvironmentalReverb2 = NULL;
    }

    if (engineObject2 != NULL) {
        (*engineObject2)->Destroy(engineObject2);
        engineObject2 = NULL;
        engineEngine2 = NULL;
    }
}


/*JNI_FALSE=0 JNI_SUCCESS=1*/
int createOpenslEngine2() {
    SLresult result;
    //线程安全
    const SLEngineOption engineOptions[1] = {{(SLuint32) SL_ENGINEOPTION_THREADSAFE, (SLuint32) SL_BOOLEAN_TRUE}};
    //该函数表示：初始化引擎对象给使用者一个处理手柄对象，第四个参数（需要支持的interface数目）为零则会忽视第五、第六个参数
    result = slCreateEngine(&engineObject2, 1, engineOptions, 0, NULL, NULL);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("opensl es引擎创建初始化失败");
        return JNI_FALSE;
    }

    //该函数表示：转化一个Object从未实例化到实例化过程，第二个参数表示是否异步
    result = (*engineObject2)->Realize(engineObject2, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("引擎Object实例化失败");
        return JNI_FALSE;
    }

    //该函数表示：得到由Object暴露的接口，这里指的是引擎接口，第二个参数是接口ID,第三个参数是输出的引擎接口对象
    result = (*engineObject2)->GetInterface(engineObject2, SL_IID_ENGINE, &engineEngine2);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("引擎接口获取失败");
        return JNI_FALSE;
    }

    //该函数表示：创建输出混音器--->由引擎接口创建，从第三个参数开始就是支持的interface数目，同样的为零忽略第四第五个参数
    const SLInterfaceID interfaceIds[1] = {SL_IID_ENVIRONMENTALREVERB};//这里给一个环境混响的接口id
    const SLboolean reqs[1] = {SL_BOOLEAN_TRUE};
    result = (*engineEngine2)->CreateOutputMix(engineEngine2, &outputMixObject2, 1, interfaceIds,
                                              reqs);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("创建输出混音器失败");
        return JNI_FALSE;
    }

    //同样的实例化输出混音器对象
    result = (*outputMixObject2)->Realize(outputMixObject2, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("输出混音器outout mix实例化失败");
        return JNI_FALSE;
    }

    //因为环境混响接口的失败与否没关系的
    //同样的申请支持了环境混响EnvironmentalReverb接口就可以获取该接口对象
    result = (*outputMixObject2)->GetInterface(outputMixObject2, SL_IID_ENVIRONMENTALREVERB,
                                              &outputMixEnvironmentalReverb2);
    if (result == SL_RESULT_SUCCESS) {
        result = (*outputMixEnvironmentalReverb2)->SetEnvironmentalReverbProperties(
                outputMixEnvironmentalReverb2, &reverbSettings2);
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
int createBufferQueue2(int sampleRate, int channels) {
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
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject2};
    SLDataSink audioSnk = {&loc_outmix, NULL};

    // create audio player
    const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    result = (*engineEngine2)->CreateAudioPlayer(engineEngine2, &bqPlayerObject2, &audioSrc, &audioSnk,
                                                1, ids, req);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("创建audioplayer失败");
        return JNI_FALSE;
    }


    result = (*bqPlayerObject2)->Realize(bqPlayerObject2, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("实例化audioplayer失败");
        return JNI_FALSE;
    }

    LOGD("---createBufferQueueAudioPlayer---");

    // get the play interface
    result = (*bqPlayerObject2)->GetInterface(bqPlayerObject2, SL_IID_PLAY, &bqPlayerPlay2);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("获取play接口对象失败");
        return JNI_FALSE;
    }

    // get the buffer queue interface
    result = (*bqPlayerObject2)->GetInterface(bqPlayerObject2, SL_IID_BUFFERQUEUE,
                                             &bqPlayerBufferQueue2);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("获取BUFFERQUEUE接口对象失败");
        return JNI_FALSE;
    }

    // register callback on the buffer queue
    result = (*bqPlayerBufferQueue2)->RegisterCallback(bqPlayerBufferQueue2, bqPlayerCallback2,
                                                      (void *) "1");
    if (result != SL_RESULT_SUCCESS) {
        LOGD("获取play接口对象失败");
        return JNI_FALSE;
    }

    // set the player's state to playing
    result = (*bqPlayerPlay2)->SetPlayState(bqPlayerPlay2, SL_PLAYSTATE_PLAYING);
    if (result != SL_RESULT_SUCCESS) {
        LOGD("设置为可播放状态失败");
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

void getPCM(void **pcm, size_t *size){
    int out_channer_nb = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    while (av_read_frame(avFormatContext2, avPacket2) >= 0) {
        if (avPacket2->stream_index == streamIndex2) {
            int ret = avcodec_send_packet(avCodecContext2, avPacket2);
            currentIndex2 ++;
            if (ret >= 0) {
                ret = avcodec_receive_frame(avCodecContext2, avFrame2);
                LOGE("解码 currentIndex2 = %d",currentIndex2);
                swr_convert(swrContext2, &out_buffer2, 48000 * 2, (const uint8_t **) avFrame2->data, avFrame2->nb_samples);
                bufferSize2 = av_samples_get_buffer_size(NULL, out_channer_nb, avFrame2->nb_samples,AV_SAMPLE_FMT_S16, 1);
                *pcm = out_buffer2;
                *size = bufferSize2;
            }
            break;
        }
    }
}

// 当喇叭播放完声音时回调此方法
void bqPlayerCallback2(SLAndroidSimpleBufferQueueItf bq, void *context)
{

    char * args = (char *)context;
    if (strcmp(args, "1") == 0){
        LOGE("来自缓冲的回调");
    } else {
        LOGE("主动触发");
    }


    bufferLen = 0;
    //assert(NULL == context);
    getPCM(&buffer, &bufferLen);
    // for streaming playback, replace this test by logic to find and fill the next buffer
    if (NULL != buffer && 0 != bufferLen) {
        SLresult result;
        // enqueue another buffer
        result = (*bqPlayerBufferQueue2)->Enqueue(bqPlayerBufferQueue2, out_buffer2, bufferSize2);
        // the most likely other result is SL_RESULT_BUFFER_INSUFFICIENT,
        // which for this code example would indicate a programming error
        if (result != SL_RESULT_SUCCESS) {
            LOGD("入队失败");
        } else {
            LOGD("入队成功");
        }

    }
}



}
extern "C"
JNIEXPORT void JNICALL
Java_com_jason_ndk_ffmpeg_openeles_MainActivity_stop2(JNIEnv *env, jobject thiz) {
    releaseResource2();
}