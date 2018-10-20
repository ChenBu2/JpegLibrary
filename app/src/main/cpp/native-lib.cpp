#include <jni.h>
#include <string>
#include <android/log.h>
#include <android/bitmap.h>
#include <malloc.h>
#include <setjmp.h>

extern "C" {
#include "include/jpeglib.h"
#include "include/cdjpeg.h"
#include "include/transupp.h"
}

typedef uint8_t BYTE;
#define TAG "image "
#define LOGE(...) __android_log_print(ANDROID_LOG_INFO,TAG,__VA_ARGS__)

//#define TRUE 1
//#define FALSE 0

extern "C"
int generateJPEG(BYTE *data, int w, int h, int quality, const char *outfilename, jboolean optimize);

extern "C"
unsigned char* ReadJpeg(const char* path, int& width, int& height);

extern "C"
unsigned char* do_Stretch_Linear(int w_Dest,int h_Dest,int bit_depth,unsigned char *src,int w_Src,int h_Src);

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_kanche_mars_util_JpegUtils_compressBitmap(JNIEnv *env, jclass type, jobject bitmap,
                                                jint width, jint height, jstring fileName,
                                                jint quality) {

    AndroidBitmapInfo infoColor;

    BYTE *pixelColor;
    BYTE *data;
    BYTE *tempData;
    const char *filename = env->GetStringUTFChars(fileName, 0);

    if ((AndroidBitmap_getInfo(env, bitmap, &infoColor)) < 0) {
        LOGE("解析错误");
        return FALSE;
    }

    if ((AndroidBitmap_lockPixels(env, bitmap, (void **) &pixelColor)) < 0) {
        LOGE("加载失败");
        return FALSE;
    }

    BYTE r, g, b;
    int color;
    data = (BYTE *) malloc(width * height * 3);
    tempData = data;
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            color = *((int *) pixelColor);
            r = ((color & 0x00FF0000) >>
                 16);//与操作获得rgb，参考java Color定义alpha color >>> 24 red (color >> 16) & 0xFF
            g = ((color & 0x0000FF00) >> 8);
            b = color & 0X000000FF;

            *data = b;
            *(data + 1) = g;
            *(data + 2) = r;
            data += 3;
            pixelColor += 4;
        }
    }

    AndroidBitmap_unlockPixels(env, bitmap);
    int resultCode = generateJPEG(tempData, width, height, quality, filename, TRUE);

    free(tempData);
    if (resultCode == 0) {
        return FALSE;
    }

    return TRUE;
}

//生成图片的缩略图（图片的一个缩小版本）
extern "C"
JNIEXPORT jboolean JNICALL
Java_com_kanche_mars_util_JpegUtils_generateThumbnail(JNIEnv *env, jclass type, jstring inputFile, jstring outputFile,
                                                        jint targetWidth, jint targetHeight)
{
    if(inputFile == NULL || outputFile == NULL) return FALSE;

    //读取jpeg图片像素数组
    int w=0,h=0;
    const char *infile = env->GetStringUTFChars(inputFile, 0);
    unsigned char* buff = ReadJpeg(infile,w,h);
    if(buff == NULL) {
        printf("ReadJpeg Failed\n");
        return FALSE;
    }

    //缩放图片
    unsigned char * img_buf = do_Stretch_Linear(targetWidth, targetHeight, 24, buff, w, h);
    free(buff);

    //将缩放后的像素数组保存到jpeg文件
    const char *outfile = env->GetStringUTFChars(outputFile, 0);
    bool bRetWrite = generateJPEG(img_buf, targetWidth, targetHeight, 95, outfile, TRUE);
    delete[] img_buf;

    if(bRetWrite){
        return TRUE;
    }else{
        printf("GenerateImageThumbnail: write failed\n");
        return TRUE;
    }
}

//图片压缩方法
extern "C"
int generateJPEG(BYTE *data, int w, int h, int quality,
                 const char *outfilename, jboolean optimize) {
    int nComponent = 3;

    struct jpeg_compress_struct jcs;

    struct jpeg_error_mgr jem;

    jcs.err = jpeg_std_error(&jem);

    //为JPEG对象分配空间并初始化
    jpeg_create_compress(&jcs);
    //获取文件信息
    FILE *f = fopen(outfilename, "wb");
    if (f == NULL) {
        return 0;
    }
    //指定压缩数据源
    jpeg_stdio_dest(&jcs, f);
    jcs.image_width = w;//image_width->JDIMENSION->typedef unsigned int
    jcs.image_height = h;

    jcs.arith_code = FALSE;
    //input_components为1代表灰度图，在等于3时代表彩色位图图像
    jcs.input_components = nComponent;
    if (nComponent == 1)
        //in_color_space为JCS_GRAYSCALE表示灰度图，在等于JCS_RGB时代表彩色位图图像
        jcs.in_color_space = JCS_GRAYSCALE;
    else
        jcs.in_color_space = JCS_RGB;

    jpeg_set_defaults(&jcs);
    //optimize_coding为TRUE，将会使得压缩图像过程中基于图像数据计算哈弗曼表，由于这个计算会显著消耗空间和时间，默认值被设置为FALSE。
    jcs.optimize_coding = optimize ? TRUE : FALSE;
    //为压缩设定参数，包括图像大小，颜色空间
    jpeg_set_quality(&jcs, quality, TRUE);
    //开始压缩
    jpeg_start_compress(&jcs, TRUE);

    JSAMPROW row_pointer[1];//JSAMPROW就是一个字符型指针 定义一个变量就等价于=========unsigned char *temp
    int row_stride;
    row_stride = jcs.image_width * nComponent;
    while (jcs.next_scanline < jcs.image_height) {
        row_pointer[0] = &data[jcs.next_scanline * row_stride];
        //写入数据 http://www.cnblogs.com/darkknightzh/p/4973828.html
        jpeg_write_scanlines(&jcs, row_pointer, 1);
    }

    //压缩完毕
    jpeg_finish_compress(&jcs);
    //释放资源
    jpeg_destroy_compress(&jcs);
    fclose(f);

    return 1;
}

struct my_error_mgr {
    struct jpeg_error_mgr pub;	/* "public" fields */

    jmp_buf setjmp_buffer;	/* for return to caller */
};

typedef struct my_error_mgr * my_error_ptr;

METHODDEF(void)
my_error_exit (j_common_ptr cinfo)
{
    /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
    my_error_ptr myerr = (my_error_ptr) cinfo->err;

    /* Always display the message. */
    /* We could postpone this until after returning, if we chose. */
    (*cinfo->err->output_message) (cinfo);

    /* Return control to the setjmp point */
    longjmp(myerr->setjmp_buffer, 1);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_kanche_mars_util_JpegUtils_cropJpg(JNIEnv *env, jclass type, jstring inputFile, jstring outputFile,
                                         jint width, jint height)
{
    const char* infile = env->GetStringUTFChars(inputFile, 0);
    const char* outfile = env->GetStringUTFChars(outputFile, 0);

    struct jpeg_decompress_struct srcinfo;
    struct jpeg_compress_struct dstinfo;
    struct jpeg_error_mgr jsrcerr, jdsterr;

    jvirt_barray_ptr * src_coef_arrays;
    jvirt_barray_ptr * dst_coef_arrays;
    FILE * input_file;
    FILE * output_file;
    JCOPY_OPTION copyoption;
    jpeg_transform_info transformoption;
    struct my_error_mgr jerr_src, jerr_dst;

    copyoption = JCOPYOPT_ALL;

    transformoption.transform = JXFORM_NONE;
    transformoption.trim = TRUE;
    transformoption.force_grayscale = FALSE;
    transformoption.crop = TRUE;

    transformoption.crop_width = (JDIMENSION)width;
    transformoption.crop_width_set = JCROP_POS;
    transformoption.crop_height = (JDIMENSION)height;
    transformoption.crop_height_set = JCROP_POS;
    transformoption.output_width = (JDIMENSION)width;
    transformoption.output_height = (JDIMENSION)height;

    srcinfo.err = jpeg_std_error(&jerr_src.pub);
    jerr_src.pub.error_exit = my_error_exit;
    if (setjmp(jerr_src.setjmp_buffer)) {
        jpeg_destroy_decompress(&srcinfo);
        return 2;
    }
    srcinfo.err->trace_level = 0;
    jpeg_create_decompress(&srcinfo);

    dstinfo.err = jpeg_std_error(&jerr_dst.pub);
    jerr_dst.pub.error_exit = my_error_exit;
    if (setjmp(jerr_dst.setjmp_buffer)) {
        jpeg_destroy_compress(&dstinfo);
        return 3;
    }
    dstinfo.err->trace_level = 0;
    jpeg_create_compress(&dstinfo);
    jsrcerr.trace_level = jdsterr.trace_level;
    srcinfo.mem->max_memory_to_use = dstinfo.mem->max_memory_to_use;

    if ((input_file = fopen(infile, READ_BINARY)) == NULL) {
        return 4;
    }
    if ((output_file = fopen(outfile, WRITE_BINARY)) == NULL) {
        fclose(input_file);
        return 5;
    }

    jpeg_stdio_src(&srcinfo, input_file);
    jcopy_markers_setup(&srcinfo, copyoption);
    (void)jpeg_read_header(&srcinfo, TRUE);

    int src_width = srcinfo.image_width;
    int src_height = srcinfo.image_height;

    if (src_width > width) {
        transformoption.crop_xoffset = (JDIMENSION)(src_width - width) / 2;
        transformoption.crop_xoffset_set = JCROP_POS;
    }
    if (src_height > height) {
        transformoption.crop_yoffset = (JDIMENSION)(src_height - height) / 2;
        transformoption.crop_yoffset_set = JCROP_POS;
    }

    jtransform_request_workspace(&srcinfo, &transformoption);
    src_coef_arrays = jpeg_read_coefficients(&srcinfo);
    jpeg_copy_critical_parameters(&srcinfo, &dstinfo);
    dstinfo.write_JFIF_header = FALSE;  // for Exif format
    dst_coef_arrays = jtransform_adjust_parameters(&srcinfo, &dstinfo,
                                                   src_coef_arrays, &transformoption);
    jpeg_stdio_dest(&dstinfo, output_file);
    jpeg_write_coefficients(&dstinfo, dst_coef_arrays);
    jcopy_markers_execute(&srcinfo, &dstinfo, copyoption);
    jtransform_execute_transform(&srcinfo, &dstinfo,
                                 src_coef_arrays, &transformoption);
    jpeg_finish_compress(&dstinfo);
    jpeg_destroy_compress(&dstinfo);
    (void) jpeg_finish_decompress(&srcinfo);
    jpeg_destroy_decompress(&srcinfo);

    fclose(input_file);
    fclose(output_file);

    return 0;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_kanche_mars_util_JpegUtils_rotateJpg(JNIEnv *env, jclass type, jstring inputFile, jstring outputFile,
                                                   jint angle)
{
    const char* infile = env->GetStringUTFChars(inputFile, 0);
    const char* outfile = env->GetStringUTFChars(outputFile, 0);

    struct jpeg_decompress_struct srcinfo;
    struct jpeg_compress_struct dstinfo;
    struct jpeg_error_mgr jsrcerr, jdsterr;

    jvirt_barray_ptr * src_coef_arrays;
    jvirt_barray_ptr * dst_coef_arrays;
    FILE * input_file;
    FILE * output_file;
    JCOPY_OPTION copyoption;
    jpeg_transform_info transformoption;
    struct my_error_mgr jerr_src, jerr_dst;

    copyoption = JCOPYOPT_ALL;
    transformoption.trim = FALSE;
    transformoption.force_grayscale = FALSE;

    switch(angle){
        case 0:
            return 1;
        case 1:
            transformoption.transform = JXFORM_ROT_90;
            break;
        case 2:
            transformoption.transform = JXFORM_ROT_180;
            break;
        case 3:
            transformoption.transform = JXFORM_ROT_270;
            break;
        default:
            return 1;
    }
    srcinfo.err = jpeg_std_error(&jerr_src.pub);
    jerr_src.pub.error_exit = my_error_exit;
    if (setjmp(jerr_src.setjmp_buffer)) {
        jpeg_destroy_decompress(&srcinfo);
        return 2;
    }
    srcinfo.err->trace_level = 0;
    jpeg_create_decompress(&srcinfo);

    dstinfo.err = jpeg_std_error(&jerr_dst.pub);
    jerr_dst.pub.error_exit = my_error_exit;
    if (setjmp(jerr_dst.setjmp_buffer)) {
        jpeg_destroy_compress(&dstinfo);
        return 3;
    }
    dstinfo.err->trace_level = 0;
    jpeg_create_compress(&dstinfo);
    jsrcerr.trace_level = jdsterr.trace_level;
    srcinfo.mem->max_memory_to_use = dstinfo.mem->max_memory_to_use;

    if ((input_file = fopen(infile, READ_BINARY)) == NULL) {
        return 4;
    }
    if ((output_file = fopen(outfile, WRITE_BINARY)) == NULL) {
        fclose(input_file);
        return 5;
    }

    jpeg_stdio_src(&srcinfo, input_file);
    jcopy_markers_setup(&srcinfo, copyoption);
    (void)jpeg_read_header(&srcinfo, TRUE);
    jtransform_request_workspace(&srcinfo, &transformoption);
    src_coef_arrays = jpeg_read_coefficients(&srcinfo);
    jpeg_copy_critical_parameters(&srcinfo, &dstinfo);
    dstinfo.write_JFIF_header = FALSE;  // for Exif format
    dst_coef_arrays = jtransform_adjust_parameters(&srcinfo, &dstinfo,
                                                   src_coef_arrays, &transformoption);
    jpeg_stdio_dest(&dstinfo, output_file);
    jpeg_write_coefficients(&dstinfo, dst_coef_arrays);
    jcopy_markers_execute(&srcinfo, &dstinfo, copyoption);
    jtransform_execute_transform(&srcinfo, &dstinfo,
                                      src_coef_arrays, &transformoption);
    jpeg_finish_compress(&dstinfo);
    jpeg_destroy_compress(&dstinfo);
    (void) jpeg_finish_decompress(&srcinfo);
    jpeg_destroy_decompress(&srcinfo);

    fclose(input_file);
    fclose(output_file);

    return 0;
}

extern "C"
unsigned char* ReadJpeg(const char* path, int& width, int& height)
{
    FILE *file = fopen( path, "rb" );
    if ( file == NULL )	{
        return NULL;
    }

    struct jpeg_decompress_struct info; //for our jpeg info

// 	struct jpeg_error_mgr err; //the error handler
// 	info.err = jpeg_std_error(&err);

    struct my_error_mgr my_err;

    info.err = jpeg_std_error(&my_err.pub);
    my_err.pub.error_exit = my_error_exit;

    /* Establish the setjmp return context for my_error_exit to use. */
    if (setjmp(my_err.setjmp_buffer)) {
        /* If we get here, the JPEG code has signaled an error.
        * We need to clean up the JPEG object, close the input file, and return.
        */
        printf("Error occured\n");
        jpeg_destroy_decompress(&info);
        fclose(file);
        return NULL;
    }

    jpeg_create_decompress( &info ); //fills info structure
    jpeg_stdio_src( &info, file );        //void

    int ret_Read_Head = jpeg_read_header( &info, TRUE); //int

    if(ret_Read_Head != JPEG_HEADER_OK){
        printf("jpeg_read_header failed\n");
        fclose(file);
        jpeg_destroy_decompress(&info);
        return NULL;
    }

    bool bStart = jpeg_start_decompress( &info );
    if(!bStart){
        printf("jpeg_start_decompress failed\n");
        fclose(file);
        jpeg_destroy_decompress(&info);
        return NULL;
    }
    int w = width = info.output_width;
    int h = height = info.output_height;
    int numChannels = info.num_components; // 3 = RGB, 4 = RGBA
    unsigned long dataSize = w * h * numChannels;

    // read RGB(A) scanlines one at a time into jdata[]
    unsigned char *data = (unsigned char *)malloc( dataSize );
    if(!data) return NULL;

    unsigned char* rowptr;
    while ( info.output_scanline < h )
    {
        rowptr = data + info.output_scanline * w * numChannels;
        jpeg_read_scanlines( &info, &rowptr, 1 );
    }

    jpeg_finish_decompress( &info );

    fclose( file );

    return data;
}

/*参数为：
 *返回图片的宽度(w_Dest),
 *返回图片的高度(h_Dest),
 *返回图片的位深(bit_depth),
 *源图片的RGB数据(src),
 *源图片的宽度(w_Src),
 *源图片的高度(h_Src)
 */
extern "C"
unsigned char* do_Stretch_Linear(int w_Dest,int h_Dest,int bit_depth,unsigned char *src,int w_Src,int h_Src)
{
    int sw = w_Src-1, sh = h_Src-1, dw = w_Dest-1, dh = h_Dest-1;
    int B, N, x, y;
    int nPixelSize = bit_depth/8;
    unsigned char *pLinePrev,*pLineNext;
    unsigned char *pDest = new unsigned char[w_Dest*h_Dest*bit_depth/8];
    unsigned char *tmp;
    unsigned char *pA,*pB,*pC,*pD;

    for(int i=0;i<=dh;++i)
    {
        tmp =pDest + i*w_Dest*nPixelSize;
        y = i*sh/dh;
        N = dh - i*sh%dh;
        pLinePrev = src + (y++)*w_Src*nPixelSize;
        //pLinePrev =(unsigned char *)aSrc->m_bitBuf+((y++)*aSrc->m_width*nPixelSize);
        pLineNext = (N==dh) ? pLinePrev : src+y*w_Src*nPixelSize;
        //pLineNext = ( N == dh ) ? pLinePrev : (unsigned char *)aSrc->m_bitBuf+(y*aSrc->m_width*nPixelSize);
        for(int j=0;j<=dw;++j)
        {
            x = j*sw/dw*nPixelSize;
            B = dw-j*sw%dw;
            pA = pLinePrev+x;
            pB = pA+nPixelSize;
            pC = pLineNext + x;
            pD = pC + nPixelSize;
            if(B == dw)
            {
                pB=pA;
                pD=pC;
            }

            for(int k=0;k<nPixelSize;++k)
            {
                *tmp++ = ( unsigned char )( int )(
                        ( B * N * ( *pA++ - *pB - *pC + *pD ) + dw * N * *pB++
                          + dh * B * *pC++ + ( dw * dh - dh * B - dw * N ) * *pD++
                          + dw * dh / 2 ) / ( dw * dh ) );
            }
        }
    }
    return pDest;
}

extern "C"
jstring
Java_com_nick_compress_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}
