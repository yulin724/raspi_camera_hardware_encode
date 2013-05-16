#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <linux/videodev2.h>

#include "v4l2uvc.h"
#include "bcm_host.h"
#include "ilclient.h"

#define NUMFRAMES 300
#define WIDTH 320
#define HEIGHT 240

#define HEIGHT16  ((HEIGHT+15)&~15)
#define SIZE      ((WIDTH * HEIGHT16 * 3)/2)

struct vdIn *videoIn;

int framenumber = 0;

typedef unsigned char uint8_t;

	void 
YUYVToNV12(const void* yuyv, void *nv12, int width, int height)
{
	uint8_t* Y	= (uint8_t*)nv12;
	uint8_t* UV = (uint8_t*)Y + width * height;

	int i;
	int j;

	for(i = 0; i < height; i += 2)
	{
		for (j = 0; j < width; j++)
		{
			*(uint8_t*)((uint8_t*)Y + i * width + j) = *(uint8_t*)((uint8_t*)yuyv + i * width * 2 + j * 2);
			*(uint8_t*)((uint8_t*)Y + (i + 1) * width + j) = *(uint8_t*)((uint8_t*)yuyv + (i + 1) * width * 2 + j * 2);
			*(uint8_t*)((uint8_t*)UV + ((i * width) >> 1) + j) = *(uint8_t*)((uint8_t*)yuyv + i * width * 2 + j * 2 + 1);
		}
	}
}

	void
print_def(OMX_PARAM_PORTDEFINITIONTYPE def)
{
	printf("Port %lu: %s %lu/%lu %lu %lu %s,%s,%s %lux%lu %lux%lu @%lu %u\n",
			def.nPortIndex,
			def.eDir == OMX_DirInput ? "in" : "out",
			def.nBufferCountActual,
			def.nBufferCountMin,
			def.nBufferSize,
			def.nBufferAlignment,
			def.bEnabled ? "enabled" : "disabled",
			def.bPopulated ? "populated" : "not pop.",
			def.bBuffersContiguous ? "contig." : "not cont.",
			def.format.video.nFrameWidth,
			def.format.video.nFrameHeight,
			def.format.video.nStride,
			def.format.video.nSliceHeight,
			def.format.video.xFramerate, def.format.video.eColorFormat);
}

	static int
video_encode_test(char *outputfilename)
{
	OMX_VIDEO_PARAM_PORTFORMATTYPE format;
	OMX_PARAM_PORTDEFINITIONTYPE def;
	COMPONENT_T *video_encode = NULL;
	COMPONENT_T *list[5];
	OMX_BUFFERHEADERTYPE *buf;
	OMX_BUFFERHEADERTYPE *out;
	OMX_ERRORTYPE r;
	ILCLIENT_T *client;
	int status = 0;
	FILE *outf;

	memset(list, 0, sizeof(list));

	if ((client = ilclient_init()) == NULL) {
		return -3;
	}

	if (OMX_Init() != OMX_ErrorNone) {
		ilclient_destroy(client);
		return -4;
	}

	// create video_encode
	r = ilclient_create_component(client, &video_encode, "video_encode",
			ILCLIENT_DISABLE_ALL_PORTS |
			ILCLIENT_ENABLE_INPUT_BUFFERS |
			ILCLIENT_ENABLE_OUTPUT_BUFFERS);
	if (r != 0) {
		printf
			("ilclient_create_component() for video_encode failed with %x!\n",
			 r);
		exit(1);
	}
	list[0] = video_encode;

	memset(&def, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
	def.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	def.nVersion.nVersion = OMX_VERSION;
	def.nPortIndex = 200;

	if (OMX_GetParameter
			(ILC_GET_HANDLE(video_encode), OMX_IndexParamPortDefinition,
			 &def) != OMX_ErrorNone) {
		printf("%s:%d: OMX_GetParameter() for video_encode port 200 failed!\n",
				__FUNCTION__, __LINE__);
		exit(1);
	}

	print_def(def);

	// Port 200: in 1/1 115200 16 enabled,not pop.,not cont. 320x240 320x240 @1966080 20
	def.format.video.nFrameWidth = WIDTH;
	def.format.video.nFrameHeight = HEIGHT;
	def.format.video.xFramerate = 30 << 16;
	def.format.video.nSliceHeight = def.format.video.nFrameHeight;
	def.format.video.nStride = def.format.video.nFrameWidth;
	def.format.video.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;

	print_def(def);

	r = OMX_SetParameter(ILC_GET_HANDLE(video_encode),
			OMX_IndexParamPortDefinition, &def);
	if (r != OMX_ErrorNone) {
		printf
			("%s:%d: OMX_SetParameter() for video_encode port 200 failed with %x!\n",
			 __FUNCTION__, __LINE__, r);
		exit(1);
	}

	memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
	format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
	format.nVersion.nVersion = OMX_VERSION;
	format.nPortIndex = 201;
	format.eCompressionFormat = OMX_VIDEO_CodingAVC;

	printf("OMX_SetParameter for video_encode:201...\n");
	r = OMX_SetParameter(ILC_GET_HANDLE(video_encode),
			OMX_IndexParamVideoPortFormat, &format);
	if (r != OMX_ErrorNone) {
		printf
			("%s:%d: OMX_SetParameter() for video_encode port 201 failed with %x!\n",
			 __FUNCTION__, __LINE__, r);
		exit(1);
	}

	printf("encode to idle...\n");
	if (ilclient_change_component_state(video_encode, OMX_StateIdle) == -1) {
		printf
			("%s:%d: ilclient_change_component_state(video_encode, OMX_StateIdle) failed",
			 __FUNCTION__, __LINE__);
	}

	printf("enabling port buffers for 200...\n");
	if (ilclient_enable_port_buffers(video_encode, 200, NULL, NULL, NULL) != 0) {
		printf("enabling port buffers for 200 failed!\n");
		exit(1);
	}

	printf("enabling port buffers for 201...\n");
	if (ilclient_enable_port_buffers(video_encode, 201, NULL, NULL, NULL) != 0) {
		printf("enabling port buffers for 201 failed!\n");
		exit(1);
	}

	printf("encode to executing...\n");
	ilclient_change_component_state(video_encode, OMX_StateExecuting);

	outf = fopen(outputfilename, "w");
	if (outf == NULL) {
		printf("Failed to open '%s' for writing video\n", outputfilename);
		exit(1);
	}

	printf("looping for buffers...\n");
	do {
		buf = ilclient_get_input_buffer(video_encode, 200, 1);
		if (buf == NULL) {
			printf("Doh, no buffers for me!\n");
		}
		else {
			// grab image from WebCam
			if (uvcGrab (videoIn) < 0) {
				fprintf (stderr, "Error grabbing\n");
				close_v4l2 (videoIn);
				free (videoIn);
				exit (1);
			}
		
			// before hardware encode, convert to 420Planar
			YUYVToNV12(videoIn->framebuffer, buf->pBuffer, WIDTH, HEIGHT);
			framenumber++;
			buf->nFilledLen = SIZE;

			if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_encode), buf) !=
					OMX_ErrorNone) {
				printf("Error emptying buffer!\n");
			}

			out = ilclient_get_output_buffer(video_encode, 201, 1);

			r = OMX_FillThisBuffer(ILC_GET_HANDLE(video_encode), out);
			if (r != OMX_ErrorNone) {
				printf("Error filling buffer: %x\n", r);
			}

			if (out != NULL) {
				if (out->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
					int i;
					for (i = 0; i < out->nFilledLen; i++)
						printf("%x ", out->pBuffer[i]);
					printf("\n");
				}

				r = fwrite(out->pBuffer, 1, out->nFilledLen, outf);
				if (r != out->nFilledLen) {
					printf("fwrite: Error emptying buffer: %d!\n", r);
				}
				else {
					printf("Writing frame %d/%d\n", framenumber, NUMFRAMES);
				}
				out->nFilledLen = 0;
			}
			else {
				printf("Not getting it :(\n");
			}

		}
	}
	while (framenumber < NUMFRAMES);

	fclose(outf);

	printf("Teardown.\n");

	printf("disabling port buffers for 200 and 201...\n");
	ilclient_disable_port_buffers(video_encode, 200, NULL, NULL, NULL);
	ilclient_disable_port_buffers(video_encode, 201, NULL, NULL, NULL);

	ilclient_state_transition(list, OMX_StateIdle);
	ilclient_state_transition(list, OMX_StateLoaded);

	ilclient_cleanup_components(list);

	OMX_Deinit();

	ilclient_destroy(client);

	return status;
}

	void 
init_v4l2() {
	char *videodevice = "/dev/video0";
	int width = WIDTH;
	int height = HEIGHT;
	int format = V4L2_PIX_FMT_YUYV;
	int grabmethod = 1;

	videoIn = (struct vdIn *) calloc (1, sizeof (struct vdIn));
	if (init_videoIn
			(videoIn, (char *) videodevice, width, height, format, grabmethod) < 0)
		exit (1);
}

	int
main(int argc, char **argv)
{
	int ret = 0;

	if (argc < 2) {
		printf("Usage: %s <filename>\n", argv[0]);
		exit(1);
	}

	bcm_host_init();

	init_v4l2();

	ret = video_encode_test(argv[1]);

	close_v4l2 (videoIn);
	free (videoIn);

	return ret;
}
