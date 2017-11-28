#include <3ds.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>

#define SOC_ALIGN       0x1000
#define SOC_BUFFERSIZE  0x100000

#define STATE_GET_USERNAME 0
#define STATE_SET_USERNAME 1
#define STATE_GET_OAUTH    2
#define STATE_SET_OAUTH    3
#define STATE_GET_CHANNEL  4
#define STATE_SET_CHANNEL  5
#define STATE_CONNECTING   6
#define STATE_CONNECTED    7

char* ircServer = "irc.chat.twitch.tv";

char username[256] = "dennisvb_";
char channel[256] = "drdisrespectlive";
char oauth[256] = "oauth:jczooyrfnvb0ux1rpcezx5ir99ezm4";

u32 key_down;
u32 key_held;
u32 key_up;
touchPosition touch;
touchPosition last_touch;

static SwkbdState swkbd;
static char swkbdBuffer[60];
SwkbdButton swkbdButton = SWKBD_BUTTON_NONE;
bool swkbdOpen = false;

static void *SOC_buffer = NULL;

int sockfd;

void get_input() {
    hidScanInput();

    key_down = hidKeysDown();
    key_held = hidKeysHeld();
    key_up = hidKeysUp();
    last_touch = touch;
    hidTouchRead(&touch);
}

void keyboard_open(char *initial) {
    swkbdOpen = true;
	swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 3, -1);
	swkbdSetInitialText(&swkbd, swkbdBuffer);
    swkbdSetHintText(&swkbd, initial);
	swkbdButton = swkbdInputText(&swkbd, swkbdBuffer, sizeof(swkbdBuffer));
}

void parse_irc(char* str) {
    char out[1024] = {};
    
    //strtok(str, ":");
    if (strstr(str, "PING") == str) {
        sprintf(out, "PONG %s\n", str+5);
        send(sockfd, out, strlen(out), 0);
        return;
    }
    str++;
    char* source = strtok(str, " ");
    
    if(strstr(source, "!")) {
        strstr(source, "!")[0] = '\0';
    }
    
    char* command = strtok(NULL, " ");
    if (!strcmp(command, "PRIVMSG")) {
        char* target = strtok(NULL, " ");
        char* rest = strtok(NULL, "\r");
        if (strcmp(target+1, channel)) {
            printf("\x1b%s: %s\x1b[0m\n", source, rest+1);
        }
    }
}

int main(int argc, char **argv) {
	int state, ret;
	
    sdmcInit();
    cfguInit();
	gfxInitDefault();

	consoleInit(GFX_BOTTOM, NULL);

    mkdir("sdmc:/Twitch", 0777);
	chdir("sdmc:/Twitch");
	
	SOC_buffer = (void*) memalign(0x1000, 0x100000);
	if (SOC_buffer == NULL) return -1;

	if ((ret = socInit(SOC_buffer, SOC_BUFFERSIZE)) != 0) {
        // need to free the shared memory block if something goes wrong
        socExit();
        free(SOC_buffer);
        SOC_buffer = NULL;
        return -1;
	}
	
	int n = 0;
    char recv[8192];
    char out[1024] = {};
    struct sockaddr_in serv_addr; 
    memset(recv, 0, sizeof(recv));
    memset(&serv_addr, 0, sizeof(serv_addr));

	state = STATE_GET_USERNAME;

	bool connected = false;

	// Main loop
	while (aptMainLoop()) {
        get_input();
        if (key_down & KEY_START) break;

		if (connected) {
            memset(recv, 0, sizeof(recv));
            n = read(sockfd, recv, 8192);
            if (n < 0 && errno == EAGAIN) {
            
            } else {
                char* lines = recv;

                while (true) {
                    char* line = lines;
                    lines = strpbrk(lines, "\n");
                    line[strcspn(line, "\n")] = '\0';
                    if (line[0] == '\0') break;

                    parse_irc(line);
                    if (!lines) break;
                    lines++;
                }
            }
        }
        
        if (state == STATE_GET_USERNAME) {
            keyboard_open("Please enter your username:");
            state = STATE_SET_USERNAME;
        } else if (state == STATE_SET_USERNAME) {
            strcpy(username, swkbdBuffer);
            state = STATE_CONNECTING;
		} else if (state == STATE_CONNECTING) {
			printf("Server address: %s\n", ircServer);
        	if (!gethostbyname(ircServer)) {
				printf("Resolving address failed\n");
				continue;
			}

            if (!sockfd) {
                sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd < 0) {
                    printf("socket() failed: %d\n", errno);
                    return -1;
			    }
            }
			
			
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(6667);
            struct hostent* host_info;
            host_info = gethostbyname(ircServer);
            if (!host_info) {
                printf("gethostbyname() failed\n");
                continue;
            }
            memcpy(&serv_addr.sin_addr, host_info->h_addr_list[0], host_info->h_length);
            
            int result = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
            if(result < 0) {
                printf("connect() failed: %d\n", errno);
                return -1;
            }

            printf(">> DEBUG << Connected\n");

            sprintf(out, "PASS %s\r\n", oauth);
            send(sockfd, out, strlen(out), 0);

            sprintf(out, "NICK %s\r\n", username);
            send(sockfd, out, strlen(out), 0);

            sprintf(out, "JOIN #%s\r\n", channel);
            send(sockfd, out, strlen(out), 0);
            
            connected = true;

            result = fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);
            if (result < 0) {
                printf("fcntl failed: %d?", errno);
            }

            state = STATE_CONNECTED;
		}

		gfxFlushBuffers();
		gfxSwapBuffers();

		gspWaitForVBlank();
	}

    sdmcExit();
	gfxExit();
    cfguExit();
    socExit();

	return 0;
}
