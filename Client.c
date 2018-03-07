#include<sys/socket.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<errno.h>
#include<unistd.h>
#include<pthread.h>
#include<sys/types.h>
#include<netinet/in_systm.h>
#include<netinet/udp.h>
#include<stdint.h>
#include<sys/select.h>
#include<sys/time.h>
#include<signal.h>
#include<fcntl.h>
#include<vlc/vlc.h>
#include <cv.h>
#include <highgui.h>
#define MAXLINE 81920
#define max(a,b) a>b?a:b
enum {
	TEARDOWN=-1,PAUSE, PLAY
};
void Perror(char *msg) {
	perror(msg);
	exit(EXIT_FAILURE);
}

int udpsock, TearFlag, PauseFlag, RTPstat;
u_short Seq, MaxSeq = 0;
char FileName[32] = "output.mpg";

void *RTP(void * arg) {
	CvVideoWriter *writer = 0;
	IplImage* img = 0;
	char InBuff[MAXLINE],filename[123], *payload;
	int cc = 0,sz;
	while (RTPstat == PLAY
			&& (sz = recvfrom(udpsock, InBuff, sizeof InBuff, 0, NULL, NULL))
					> 0) {
		InBuff[sz] = 0;
		Seq = ntohs(((unsigned short *) InBuff)[1]);
		if (Seq < MaxSeq) {
			puts("opps, OutOfOrder Frame!!");
			continue;
		}
		Seq = max(Seq, MaxSeq);
		payload = InBuff + 12;

		sprintf(filename,"%d.jpeg",++cc);
		FILE *pp = fopen(filename,"w");
		write(fileno(pp),payload,sz - 12);
		fclose(pp);
		img = cvLoadImage(filename, CV_LOAD_IMAGE_COLOR);
		if (!img) {
			printf("Could not load image file: %s\n", filename);
			exit(0);
		}
		if(cc == 1)
			writer=cvCreateVideoWriter(FileName,CV_FOURCC('P','I','M','1'), 25,cvSize(img->width,img->height),1);
		cvWriteFrame(writer,img);
	}
	cvReleaseImage(&img);
	cvReleaseVideoWriter(&writer);

	if (sz < 0) {
		Perror("Reading RTP packets Problem!!");
		exit(EXIT_FAILURE);
	}
	pthread_exit(NULL);
}

int main(int argc, char **argv) {//arg[1]: Server IP, arg[2]: Server RTSP Port, arg[3]: FileName, arg[4]: RTPport
	if(argc < 4){
		puts("Error!! Must Pass 4 Args");
		exit(EXIT_FAILURE);
	}
	libvlc_instance_t * inst;
	libvlc_media_player_t *mp;
	libvlc_media_t * m;

	int ret, tcpsock, cseq = 2, sz, stat,SessionID;

	char Request[MAXLINE], Response[MAXLINE], Data[MAXLINE] , *p;

	struct sockaddr_in udp, tcp;
	socklen_t len = sizeof udp;

	memset(&tcp, 0, sizeof tcp);
	memset(&udp, 0, sizeof udp);

	tcpsock = socket(AF_INET, SOCK_STREAM, 0);
	udpsock = socket(AF_INET, SOCK_DGRAM, 0);

	tcp.sin_family = AF_INET;
	tcp.sin_port = htons(atoi(argv[2]));
	inet_pton(AF_INET, argv[1], &tcp.sin_addr);

	udp.sin_family = AF_INET;
	udp.sin_port = htons(atoi(argv[4]));
	udp.sin_addr.s_addr = htonl(INADDR_ANY);
	ret = bind(udpsock, (struct sockaddr *) &udp, len);

	if (ret < 0) {
		perror("Bind Error");
		exit(EXIT_FAILURE);
	}

	if ((ret = connect(tcpsock, (struct sockaddr *) &tcp, sizeof tcp)) < 0)
		Perror("TCP socket Problem !!");
	
	sprintf(Request,
			"SETUP rtsp://%s:%s/%s/track1 RTSP/1.0\r\nCSeq: %d\r\nTransport: RTP/AVP;unicast;client_port=%s-%d\r\n\r\n",
			argv[1], argv[2], argv[3], cseq++,argv[4],atoi(argv[4]) + 1);

	sz = write(tcpsock, Request, strlen(Request));

	if (sz < 0)
		Perror("TCP SetUp writing problem");

	sz = read(tcpsock, Response, sizeof Response);

	if (sz < 0)
		Perror("TCP SetUp reading problem");

	Response[sz] = 0;

	sscanf(Response, "%*s %d", &stat);

	if (stat == 404) {
		puts("SetUp: File Not Found at server");
		exit(EXIT_FAILURE);
	} else if (stat == 400) {
		puts("SetUp: Bad Setup Request Sent to server");
		exit(EXIT_FAILURE);
	}

	p = strstr(Response, "Session");
	
	sscanf(p, "%*s %d", &SessionID);

	inst = libvlc_new(0, NULL);
	m = libvlc_media_new_path(inst, FileName);
	mp = libvlc_media_player_new_from_media(m);
	libvlc_media_release(m);
	pthread_t tid;

	while (RTPstat != TEARDOWN) {
		puts("Please Choose an Action:\n1: Play\n2: Pause\n3: TearDown\n");
		char choice;
		scanf(" %c", &choice);
		if (choice == '1') {
			if (RTPstat == PLAY) {
				puts("State is already PLAY!!");
				continue;
			}
			RTPstat = PLAY;
			sprintf(Request,
					"PLAY rtsp://%s:%s/%s/track1 RTSP/1.0\r\nCSeq: %d\r\nSession: %d\r\n\r\n",
					argv[1], argv[2], argv[3], cseq++, SessionID);

			sz = write(tcpsock, Request, strlen(Request));
			if (sz < 0)
				Perror("TCP Play writing problem");

			sz = read(tcpsock, Response, sizeof Response);
			if (sz < 0)
				Perror("TCP Play reading problem");
			Response[sz] = 0;

			sscanf(Response, "%*s %d", &stat);

			if (stat == 404) {
				puts("Play: File Not Found at server");
				exit(EXIT_FAILURE);
			} else if (stat == 400) {
				puts("Play: Bad Setup Request Sent to server");
				exit(EXIT_FAILURE);
			}

			pthread_create(&tid, NULL, RTP, NULL);
			sleep(2);
			libvlc_media_player_play(mp);

		} else if (choice == '2') {
			if (RTPstat == PAUSE) {
				puts("State is already PAUSE!!");
				continue;
			}
			RTPstat = PAUSE;
			sprintf(Request,
					"PAUSE rtsp://%s/%s/track1 RTSP/1.0\r\nCSeq: %d\r\nSession: %d\r\n\r\n",
					argv[1], argv[3], cseq++, SessionID);

			sz = write(tcpsock, Request, strlen(Request));
			if (sz < 0)
				Perror("TCP Pause writing problem");

			sz = read(tcpsock, Response, sizeof Response);
			if (sz < 0)
				Perror("TCP Pause reading problem");
			Response[sz] = 0;

			sscanf(Response, "%*s %d", &stat);

			if (stat == 404) {
				puts("Play: File Not Found at server");
				exit(EXIT_FAILURE);
			} else if (stat == 400) {
				puts("Play: Bad Setup Request Sent to server");
				exit(EXIT_FAILURE);
			}
			RTPstat = PAUSE;
			libvlc_media_player_pause(mp);
		} else {
			sprintf(Request,
					"TEARDOWN rtsp://%s/%s/track1 RTSP/1.0\r\nCSeq: %d\r\nSession: %d\r\n\r\n",
					argv[1], argv[3], cseq++, SessionID);

			sz = write(tcpsock, Request, strlen(Request));
			if (sz < 0)
				Perror("TCP Pause writing problem");

			sz = read(tcpsock, Response, sizeof Response);
			if (sz < 0)
				Perror("TCP Pause reading problem");
			Response[sz] = 0;

			sscanf(Response, "%*s %d", &stat);

			if (stat == 404) {
				puts("Play: File Not Found at server");
				exit(EXIT_FAILURE);
			} else if (stat == 400) {
				puts("Play: Bad Setup Request Sent to server");
				exit(EXIT_FAILURE);
			}
			RTPstat = TEARDOWN;
			libvlc_media_player_stop(mp);
		}
	}
	libvlc_media_player_release(mp);
	libvlc_release(inst);
	system("rm *.jpeg");
	return 0;
}
