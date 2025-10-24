/*Stephen B Melvin Jr, <stephenbmelvin@gmail.com>
Version 0.2
*/

#ifdef __linux__
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/soundcard.h>
#include <fcntl.h>
#include "openspc.h"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <poll.h>
#include <dirent.h>
#include <termios.h>
#include <pthread.h>
#include <math.h>
#endif

#define BUF_SIZE 32000
#define max(X, Y) ((X) > (Y) ? (X) : (Y))
#define min(X, Y) ((X) < (Y) ? (X) : (Y))

int check_ext(const char *filename)
{
	long len = strlen(filename);
	const char *ext = ".spc";

	for (long i = 0; i < 4; i++) {
		if (filename[i + len - 4] != ext[i])
			return 0;
	}
	return 1;
}

static struct termios orig;
static void restore_tty(void)
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
}

struct spc700_task_args {
	void *spc_data;
	int spc_size;
	void *buf;
	int audio_fd;
	int *volume;
};

void *run_spc700_task(void *data)
{

	struct spc700_task_args *args = data;
	void *ptr = args->spc_data;
	int size = args->spc_size;
	void *buf = args->buf;
	int audio_fd = args->audio_fd;
	int *volume = args->volume;

	int fd=OSPC_Init(ptr,size);

	fcntl(STDIN_FILENO,F_SETFL,O_NONBLOCK);
	printf("[+] Playing SPC, press Enter to quit.\n");
	printf("    FRAG CONSUMED PRODUCED SPC_READ\n");

	size = 0;
	int tmp = BUF_SIZE;
	while(1)
	{
		char *buf_char = (char *)buf;
		short *buf_short = (short *)buf;
		struct pollfd p = { .fd=audio_fd, .events=POLLOUT };
		poll(&p, 1, -1);

		audio_buf_info bi;
		ioctl(audio_fd, SNDCTL_DSP_GETOSPACE, &bi);

		if (size < bi.fragsize) {
			tmp = BUF_SIZE - size;
			size += OSPC_Run(-1, (void *)(buf_char + size), tmp);
		}

		int consume_size = bi.bytes - (bi.bytes % bi.fragsize);
		consume_size = min(consume_size, size);

		float normal_vol = *volume / 100.0f;
		for (int i = 0; i < consume_size/2; i++) {
			buf_short[i] = (short)roundf(buf_short[i] * normal_vol);
		}

		// printf("%8d %8d %8ld %8ld\r", bi.fragsize, consume_size, size, tmp);
		// fflush(stdout);
		write(audio_fd, buf, consume_size);
		memmove(buf_char, buf_char + consume_size, size - consume_size);
		size -= consume_size;
	}
}

int main(int argc, char *argv[])
{
	pthread_t tid;
	struct spc700_task_args task_args;
	int optionoffset=0,audio_fd,channels=2,rformat,rchannels,format=AFMT_S16_LE,speed=32000,fd;
	int volume = 100;
	char audio_device[]="/dev/dsp";
	char ver[]="0.2";
	char c;
	void *ptr,*buf;
	off_t size;

	int frag = (8 << 16) | 8; // 8 * 256B
	long tmp;
	struct stat stat;
	char filepath_buf[4096] = {};
	char *filepath = NULL;
	DIR *dp = NULL;
	struct dirent *dirent = NULL;
	int walk_dir = 0;

	if((argc<2))
	{
		printf("\n[?] Usage: soap [sound device] SPC_FILE_NAME"
	 "\n[?] Optional parameters are in brackets."
	 "\n[?] SOAP Version %s (2003) Steve B Melvin Jr\n\n",ver);
		exit(1);
	}

	if (tcgetattr(STDIN_FILENO, &orig) == -1) {
		printf("[-] tcgetattr failed\n");
		goto err_exit_cleanup;
	}
	struct termios raw = orig;
	raw.c_lflag &= ~(ICANON | ECHO);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
		printf("[-] tcgetattr failed\n");
		goto err_exit_cleanup;
	}

	if((argc>2))
	{
		strcpy(audio_device,argv[1]);
		optionoffset++;
	}

	if((audio_fd=open(audio_device, O_WRONLY, 0)) ==-1)
		goto dsp_open_failure;

	printf("[+] Successfully opened, %s.\n",audio_device);

	rformat=format;
	if(ioctl(audio_fd, SNDCTL_DSP_SETFMT, &format) == -1)
		goto dsp_channel_failure;

	if(format!=rformat)
		goto dsp_format_failure;

	printf("[+] Successfully set sound format.\n");

	rchannels=channels;
	if(ioctl(audio_fd,SNDCTL_DSP_CHANNELS, &channels) == -1)
		goto dsp_channel_failure;

	if(channels!=rchannels)
		goto dsp_channel_failure;

	printf("[+] Successfully set channels.\n");

	if (ioctl(audio_fd, SNDCTL_DSP_SETFRAGMENT, &frag) == -1)
		goto dsp_fragment_failure;

	printf("[+] Successfully set fragments.\n");

	if(ioctl(audio_fd, SNDCTL_DSP_SPEED, &speed)==-1)
	{
		printf("[-] Could not set speed.\n");
		goto err_exit_cleanup;
	}
	printf("[+] Using speed, {%iHz}\n",speed);

	buf=malloc(BUF_SIZE);

	if (lstat(argv[1+optionoffset], &stat)) {
		printf("[-] Could not open \'%s\'\n.", argv[1]);
		goto err_exit_cleanup;
	}
	if (S_ISREG(stat.st_mode)) {
		filepath = argv[1+optionoffset];
		goto open_file;
	} else if (S_ISDIR(stat.st_mode)) {
		dp = opendir(argv[1+optionoffset]);
		if (!dp) {
			printf("[-] Could not open \'%s\'\n.", argv[1]);
			goto err_exit_cleanup;
		}
		printf("[i] \'%s\' is a directory. Play all spc files in it...\n", argv[1]);
		walk_dir = 1;
		goto get_dirent;
	} else {
		printf("[-] Invalid file \'%s\'\n.", argv[1]);
		goto err_exit_cleanup;
	}

get_dirent:
	dirent = readdir(dp);
	if (!dirent) {
		closedir(dp);
		goto program_exit;
	}
	if (!check_ext(dirent->d_name))
		goto get_dirent;

	strncpy(filepath_buf, argv[1+optionoffset], sizeof(filepath_buf));
	strncat(filepath_buf, dirent->d_name, sizeof(filepath_buf));
	filepath = filepath_buf;

	if (lstat(filepath, &stat)) {
		printf("[w] Could not open \'%s\'\n", filepath);
		goto get_dirent;
	}
	if (!S_ISREG(stat.st_mode)) {
		printf("[w] Not a regular file: \'%s\'\n", filepath);
		goto get_dirent;
	}

open_file:
	fd=open(filepath,O_RDONLY);
	if(fd<0)
	{
		printf("[-] Could not open \'%s\'\n.", filepath);
		goto err_exit_cleanup;
	}
	printf("[i] Now playing: \'%s\'...\n", filepath);

	size=lseek(fd,0,SEEK_END);
	lseek(fd,0,SEEK_SET);
	ptr=malloc(size);
	read(fd,ptr,size);
	close(fd);

	task_args.spc_data = ptr;
	task_args.spc_size = size;
	task_args.audio_fd = audio_fd;
	task_args.buf = buf;
	task_args.volume = &volume;

	if (pthread_create(&tid, NULL, run_spc700_task, &task_args) == 0) {
		fcntl(STDIN_FILENO,F_SETFL,O_NONBLOCK);
		int running = 1;
		char buf[64];
		while (running) {
			struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
			int r = poll(&pfd, 1, 200);
			if (r == 0 || !(pfd.revents & POLLIN))
				continue;

			ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));

			if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
				break;

			if (n <= 0)
				continue;

			for (ssize_t i = 0; i < n; i++) {
				char c = buf[i];
				if (c == '\n' || c == '\r') {
					running = 0;
					break;
				} else if (c == 'j') {
					volume = max(0, volume - 2);
					printf("volume level: %d%%     \r", volume);
					fflush(stdout);
				} else if (c == 'l') {
					volume = min(100, volume + 2);
					printf("volume level: %d%%     \r", volume);
					fflush(stdout);
				}
			}
		}
		pthread_cancel(tid);
		pthread_join(tid, NULL);
	}
	free(ptr);

	if (walk_dir)
		goto get_dirent;

program_exit:
	printf("\nGoodbye!\n");
	restore_tty();
	return 0;

dsp_open_failure:
	printf("[-] Could not open, %s.\n", audio_device);
	goto err_exit_cleanup;
dsp_channel_failure:
	printf("[-] Could not set channels.\n");
	goto err_exit_cleanup;
dsp_format_failure:
	printf("[-] Could not set sound format.\n");
	goto err_exit_cleanup;
dsp_fragment_failure:
	printf("[-] Could not set sound fragment.\n");
	goto err_exit_cleanup;
err_exit_cleanup:
	restore_tty();
	exit(1);
}


