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
#endif

#define BUF_SIZE 32000
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

int main(int argc, char *argv[])
{
	int optionoffset=0,audio_fd,channels=2,rformat,rchannels,format=AFMT_S16_LE,speed=32000,fd;
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
		exit(1);
	}
	printf("[+] Using speed, {%iHz}\n",speed);

	buf=malloc(BUF_SIZE);

  if (lstat(argv[1+optionoffset], &stat)) {
    printf("[-] Could not open \'%s\'\n.", argv[1]);
    exit(1);
  }
  if (S_ISREG(stat.st_mode)) {
    filepath = argv[1+optionoffset];
    goto open_file;
  } else if (S_ISDIR(stat.st_mode)) {
    dp = opendir(argv[1+optionoffset]);
    if (!dp) {
      printf("[-] Could not open \'%s\'\n.", argv[1]);
      exit(1);
    }
    printf("[i] \'%s\' is a directory. Play all spc files in it...\n", argv[1]);
    walk_dir = 1;
    goto get_dirent;
  } else {
    printf("[-] Invalid file \'%s\'\n.", argv[1]);
    exit(1);
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
		exit(1);
	}
  printf("[i] Now playing: \'%s\'...\n", filepath);

	size=lseek(fd,0,SEEK_END);
	lseek(fd,0,SEEK_SET);
	ptr=malloc(size);
	read(fd,ptr,size);
	close(fd);

	fd=OSPC_Init(ptr,size);

	free(ptr);
	fcntl(STDIN_FILENO,F_SETFL,O_NONBLOCK);
	printf("[+] Playing SPC, press Enter to quit.\n");
	printf("    FRAG CONSUMED PRODUCED SPC_READ\n");

	size = 0;
  tmp = BUF_SIZE;
	while((read(STDIN_FILENO,&c,1))<=0)
	{
		struct pollfd p = { .fd=audio_fd, .events=POLLOUT };
		poll(&p, 1, -1);
		
		audio_buf_info bi;
		ioctl(audio_fd, SNDCTL_DSP_GETOSPACE, &bi);

		if (size < bi.fragsize) {
      tmp = BUF_SIZE - size;
			size += OSPC_Run(-1, (void *)((char *)buf + size), tmp);
    }
		
		int consume_size = bi.bytes - (bi.bytes % bi.fragsize);
		consume_size = min(consume_size, size);

		printf("%8d %8d %8ld %8ld\r", bi.fragsize, consume_size, size, tmp);
		fflush(stdout);
		write(audio_fd, buf, consume_size);
		memmove(buf, buf + consume_size, size - consume_size);
		size -= consume_size;
	}

  if (walk_dir)
    goto get_dirent;

program_exit:
	printf("\nGoodbye!\n");
	return 0;

dsp_open_failure:
	printf("[-] Could not open, %s.\n", audio_device);
	exit(1);
dsp_channel_failure:
	printf("[-] Could not set channels.\n");
	exit(1);
dsp_format_failure:
	printf("[-] Could not set sound format.\n");
	exit(1);
dsp_fragment_failure:
	printf("[-] Could not set sound fragment.\n");
	exit(1);
}


