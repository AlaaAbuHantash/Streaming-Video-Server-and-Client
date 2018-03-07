#include <opencv/cv.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<errno.h>
#include<unistd.h>
#include<fcntl.h>
#include<pthread.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<netinet/udp.h>
#include<stdint.h>
#include<sys/select.h>
#include<sys/time.h>
#include<signal.h>
#include<time.h>
#include<stdlib.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#define MAXLINE 40480
#define BKT_SIZE 81920
#define LISTENQ 1024

enum {
	TEARDOWN=-1,PAUSE, PLAY
};

void Perror(char *msg) {
	perror(msg);
	exit(EXIT_FAILURE);
}
int status, RTP_server_socket, Frame_cnt;
struct sockaddr_in cli_rtp_info;
unsigned short seq_4_rtp;
struct rtp_header {
	u_short V_P_X_CC_M_PT;
	u_short SequenceNumber;
	u_int TimeStamp;
	u_int Ssrc;
};

void *Stream(void *arg) {
	char pkt[BKT_SIZE], Frame_Name[123], Frame2send[40960];
	struct rtp_header *RTP_Packet;

	int i;
	printf("%d\n", Frame_cnt);
	for (i = 1; i <= Frame_cnt; ++i) {
		while (status != PLAY) {
			if (status == TEARDOWN)
				break;
		}
		sprintf(Frame_Name, "Frame%d.jpeg", i);
		FILE *F;
		F = fopen(Frame_Name, "r");
		u_long size = read(fileno(F), Frame2send, sizeof Frame2send);
		fclose(F);
		RTP_Packet = (struct rtp_header*) pkt;
		RTP_Packet->V_P_X_CC_M_PT = htons(32794);
		RTP_Packet->SequenceNumber = htons(seq_4_rtp++);
		RTP_Packet->TimeStamp = 0;
		RTP_Packet->Ssrc = 0;
		char *payload = pkt + 12;
		memcpy(payload, Frame2send, size);
		int sz = sendto(RTP_server_socket, pkt, size + 12, 0,
				(struct sockaddr *) &cli_rtp_info, sizeof cli_rtp_info);
		if (sz < 0)
			Perror("write error at thread");
	}
	pthread_exit(NULL);
}
int main(int argc, char **argv) {

	int RTSPport = 8554,RTSP_server_socket, data, session_rand_num, IsFileFound,
			session_rec_num, cseq_rec_num, cseq_num, retVal;

	struct sockaddr_in TCP_serveraddr, cliaddr;

	char cli_req[MAXLINE], *cseq_session;
	char cli_req_type[100], Response[1000], path[1000], t[100],
			file_requested[128], client_rtp_port[32];

	if(argc == 2)
		RTSPport = atoi(argv[1]);

	RTSP_server_socket = socket(AF_INET, SOCK_STREAM, 0); // Create TCP socket
	if (RTSP_server_socket < 0)
		Perror("Error , can't create socket");

	/*Configure settings in address struct*/
	memset(&TCP_serveraddr, 0, sizeof(TCP_serveraddr));
	TCP_serveraddr.sin_family = AF_INET;
	TCP_serveraddr.sin_port = htons(RTSPport); // for example
	TCP_serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

	RTP_server_socket = socket(AF_INET, SOCK_DGRAM, 0); // Create UDP socket
	if (RTP_server_socket < 0)
		Perror("Error , can't create socket");

	/*Configure settings in address struct*/

	retVal = bind(RTSP_server_socket, (struct sockaddr *) &TCP_serveraddr,
			sizeof(TCP_serveraddr));
	if (retVal < 0)
		Perror("Error At TCP Bind");

	retVal = listen(RTSP_server_socket, LISTENQ);
	if (retVal < 0)
		Perror("Error At Listent");

	while (1) {
		Frame_cnt = 0;
		seq_4_rtp = 1;
		puts("Waiting client to request file...");
		socklen_t len = sizeof(cliaddr);
		int connfd_RTSP = accept(RTSP_server_socket,
				(struct sockaddr *) &cliaddr, &len);

		if (connfd_RTSP < 0)
			Perror("Error At Accept");

		puts("accepted Client.");
		// start receiving data  (setup)
		data = read(connfd_RTSP, cli_req, MAXLINE);
		sscanf(cli_req, "%s %s", cli_req_type, path);

		memset(Response, 0, sizeof(Response));
		//SETUP
		if (cli_req_type[0] == 'S') {

			// Read file name from path
			sscanf(path, "%*[^:]:%*[^:]:%*d/%[^/]", file_requested);

			cseq_session = strstr(cli_req, "CSeq: ");
			sscanf(cseq_session, "%*s %d", &cseq_num);

			char *aa = strstr(cli_req, "client_port=");
			sscanf(aa, "client_port=%[^-]", client_rtp_port);

			//check if the file exist or not
			if (access(file_requested, F_OK) != -1)
				IsFileFound = 1;
			else
				IsFileFound = 0;

			// generate session number
			memset(Response, 0, sizeof(Response));
			srand(time(NULL));
			session_rand_num = rand() % 10001; //To generate session# between (0-10000)

			if (IsFileFound) {
				// okay , found the file.
				sprintf(Response,
						"RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %d\r\n\r\n",
						cseq_num, session_rand_num);

				data = write(connfd_RTSP, Response, strlen(Response));
				if (data < 0)
					Perror("write error");

				CvCapture* capture = cvCaptureFromFile(file_requested);
				char name[123];
				IplImage* frame = NULL;
				do {
					frame = cvQueryFrame(capture);
					if (frame == NULL)
						break;
					sprintf(name, "Frame%d.jpeg", ++Frame_cnt);
					cvSaveImage(name, frame, 0);
				} while (frame != NULL);
				cvReleaseCapture(&capture);
				cvReleaseImage(&frame);
				puts("Done framing");
			} else {
				//not found.
				sprintf(Response,
						"RTSP/1.0 404 Not Found\r\nCSeq: %d\r\nSession: %d\r\n\r\n",
						cseq_num, session_rand_num);
				data = write(connfd_RTSP, Response, strlen(Response));
				if (data < 0)
					Perror("write error");
				continue;
			}

		} else {
			puts("Error, the first packet should be setup packet");
			// bad request.
			time_t now = time(NULL);
			struct tm h = *gmtime(&now);
			strftime(t, 100, "%a, %d %b %Y %H:%M:%S %Z", &h);
			sprintf(Response,
					"RTSP/1.0 400 Bad Request\r\nCSeq: %d\r\nDate: %s\r\n\r\n",
					cseq_num, t);

			data = write(connfd_RTSP, Response, strlen(Response));
			if (data < 0)
				Perror("write error");
			continue; // start new session with new client
		}
		memset(&cli_rtp_info, 0, sizeof(cli_rtp_info));
		cli_rtp_info.sin_family = AF_INET;
		//puts(client_rtp_port);
		//cli_rtp_info.sin_port = htons(atoi(client_rtp_port));
		cli_rtp_info.sin_port = htons(atoi(client_rtp_port));
		cli_rtp_info.sin_addr.s_addr = cliaddr.sin_addr.s_addr;

		pthread_t tid;
		pthread_create(&tid, NULL, Stream, NULL);
		fd_set rset;
		FD_ZERO(&rset);
		while (1) {
			FD_SET(connfd_RTSP, &rset);
			int max = connfd_RTSP + 1;

			int n = select(max, &rset, NULL, NULL, NULL);
			if (n < 0)
				Perror("select error");
			cseq_num++;

			//read data
			data = read(connfd_RTSP, cli_req, MAXLINE);
			sscanf(cli_req, "%s %s", cli_req_type, path);
			cseq_session = strstr(cli_req, "CSeq: ");

			//get cseq number
			sscanf(cseq_session, "%*s %d", &cseq_rec_num);

			//get session number
			char *session = strstr(cli_req, "Session: ");
			sscanf(session, "%*s %d", &session_rec_num);

			// Read file name from path
			sscanf(path, "%*[^:]:%*[^:]:%*d/%[^/]", file_requested);

			if (access(file_requested, F_OK) != -1)
				IsFileFound = 1;
			else
				IsFileFound = 0;

			memset(Response, 0, sizeof(Response));

			if (cli_req_type[0] == 'P' && cli_req_type[1] == 'L' && IsFileFound
					&& session_rand_num == session_rec_num
					&& cseq_num == cseq_rec_num) {
				//PLAY
				status = PLAY;
				sprintf(Response, "RTSP/1.0 200 OK\r\nCSeq: %d\r\n%s\r\n\r\n",
						cseq_num, session);
				data = write(connfd_RTSP, Response, strlen(Response));
				if (data < 0)
					Perror("write error");

			} else if (cli_req_type[0] == 'P' && cli_req_type[1] == 'A'
					&& IsFileFound && session_rand_num == session_rec_num
					&& cseq_num == cseq_rec_num) {
				// PAUSE
				status = PAUSE;
				sprintf(Response, "RTSP/1.0 200 OK\r\nCSeq: %d\r\n%s\r\n\r\n",
						cseq_num, session);
				data = write(connfd_RTSP, Response, strlen(Response));
				if (data < 0)
					Perror("write error");

			} else if (cli_req_type[0] == 'T' && IsFileFound
					&& session_rand_num == session_rec_num
					&& cseq_num == cseq_rec_num) {
				// TEARDOWN
				status = TEARDOWN;
				sprintf(Response, "RTSP/1.0 200 OK\r\nCSeq: %d\r\n%s\r\n\r\n",
						cseq_num, session);
				data = write(connfd_RTSP, Response, strlen(Response));
				if (data < 0)
					Perror("write error");

				puts("Thank you for using our server =D");
				close(connfd_RTSP);
				system("rm *.jpeg");
				break;

			} else {
				//Bad_request
				time_t now = time(NULL);
				struct tm h = *gmtime(&now);
				strftime(t, 100, "%a, %d %b %Y %H:%M:%S %Z", &h);
				sprintf(Response,
						"RTSP/1.0 400 Bad Request\r\nCSeq: %d\r\nDate: %s\r\n\r\n",
						cseq_num, t);
				data = write(connfd_RTSP, Response, strlen(Response));
				if (data < 0)
					Perror("write error");
				printf("Error , ");
			}

		}

	}

	close(RTSP_server_socket);
	close(RTP_server_socket);
	puts("Bye");

	return 0;
}
